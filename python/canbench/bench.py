"""Process test bench using the C memory HAL or native Python SocketCAN sockets."""
from collections import deque
import json
import os
from pathlib import Path
import select
import socket
import struct
import subprocess
import time

from .logs import Frame


class SdoAbort(RuntimeError):
    def __init__(self, code, index):
        self.code, self.index = code, index
        super().__init__(f'SDO abort 0x{code:08X} at 0x{index:04X}')


class NodeBench:
    def __init__(self, executable: Path, backend='memory', interface='vcan0'):
        if backend not in {'memory', 'socketcan'}:
            raise ValueError('Unknown bus backend')
        self.backend = backend
        self.inbox = deque()
        self.log = []
        self.events = []
        self.now_ms = 0
        self.origin = time.monotonic()
        self.sock = None
        if backend == 'socketcan':
            self.sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
            self.sock.bind((interface,))
        arguments = ['--memory'] if backend == 'memory' else ['--socketcan', interface]
        self.process = subprocess.Popen([str(executable.resolve()), *arguments], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
        try:
            while True:
                line = self._line(3)
                if line == 'READY':
                    break
                self._memory_frame(line)
        except BaseException:
            self.close()
            raise

    @property
    def timestamp(self):
        return self.now_ms / 1000 if self.backend == 'memory' else time.monotonic() - self.origin

    def _line(self, timeout):
        deadline = time.monotonic() + timeout
        data = bytearray()
        while time.monotonic() < deadline:
            remaining = max(0, deadline - time.monotonic())
            if not select.select([self.process.stdout], [], [], remaining)[0]:
                break
            character = os.read(self.process.stdout.fileno(), 1)
            if not character:
                raise RuntimeError('C node exited before protocol response')
            if character == b'\n':
                return data.decode('ascii').strip()
            data.extend(character)
        raise TimeoutError('C node response deadline exceeded')

    def _memory_frame(self, line):
        fields = line.split()
        if len(fields) < 3 or fields[0] != 'FRAME':
            raise RuntimeError(f'Unexpected C node output: {line}')
        payload = bytes.fromhex(fields[3]) if len(fields) > 3 else b''
        if len(payload) != int(fields[2]):
            raise RuntimeError('C node output DLC mismatch')
        self._record(Frame(self.timestamp, 'memory', int(fields[1], 16), payload))

    def _record(self, frame):
        self.inbox.append(frame)
        self.log.append(frame)

    def _exchange(self, command):
        self.process.stdin.write((command + '\n').encode('ascii'))
        self.process.stdin.flush()
        while True:
            line = self._line(2)
            if line.startswith('DONE '):
                return int(line.split()[1])
            self._memory_frame(line)

    def advance(self, milliseconds):
        if milliseconds < 0:
            raise ValueError('Cannot advance time backwards')
        if self.backend == 'memory':
            self.now_ms += milliseconds
            result = self._exchange(f'T {self.now_ms}')
            if result < 0:
                raise RuntimeError(f'C node tick error {result}')
        else:
            time.sleep(milliseconds / 1000)

    def send(self, identifier, payload, *, drop=False, delay_ms=0, corrupt=False):
        if drop:
            self.events.append({'fault': 'dropped', 'id': identifier, 'at': self.timestamp})
            return
        if delay_ms:
            self.events.append({'fault': 'delayed', 'id': identifier, 'delay_ms': delay_ms, 'at': self.timestamp})
            self.advance(delay_ms)
        if corrupt:
            if not payload:
                raise ValueError('Cannot corrupt an empty payload')
            payload = payload[:-1] + bytes([payload[-1] ^ 1])
            self.events.append({'fault': 'corrupted', 'id': identifier, 'at': self.timestamp})
        frame = Frame(self.timestamp, self.backend, identifier, bytes(payload))
        self.log.append(frame)
        if self.backend == 'memory':
            self._exchange(f'F {identifier:03X} {len(payload)} {payload.hex()}')
        else:
            self.sock.send(struct.pack('=IB3x8s', identifier, len(payload), payload.ljust(8, b'\x00')))

    def wait_for(self, identifier, predicate=lambda frame: True, timeout=.5):
        deadline = time.monotonic() + timeout
        virtual_deadline = self.now_ms + int(timeout * 1000)
        while True:
            while self.inbox:
                frame = self.inbox.popleft()
                if frame.can_id == identifier and predicate(frame):
                    return frame
            if self.backend == 'memory':
                if self.now_ms >= virtual_deadline or time.monotonic() >= deadline:
                    break
                self.advance(min(10, virtual_deadline - self.now_ms))
            else:
                remaining = deadline - time.monotonic()
                if remaining <= 0 or not select.select([self.sock], [], [], remaining)[0]:
                    break
                raw = self.sock.recv(16)
                raw_id, dlc, data = struct.unpack('=IB3x8s', raw)
                if raw_id & 0xC0000000:
                    continue
                self._record(Frame(self.timestamp, 'socketcan', raw_id & 0x1FFFFFFF, data[:dlc], bool(raw_id & 0x20000000)))
        raise TimeoutError(f'No matching CAN frame 0x{identifier:03X} within {timeout:g}s')

    def nmt(self, state_command, target=1):
        self.send(0, bytes([state_command, target]))

    def pdo(self, target, motor, sequence, **fault):
        payload = target.to_bytes(2, 'little') + bytes([motor, sequence])
        checksum = 0xA5
        for byte in payload:
            checksum ^= byte
        self.send(0x201, payload + bytes([checksum]), **fault)

    def sdo(self, index, value=None, size=2, subindex=0):
        if size not in {1, 2, 4}:
            raise ValueError('Expedited size must be 1, 2 or 4')
        command = 0x40 if value is None else 0x23 | ((4 - size) << 2)
        payload = bytes([command]) + index.to_bytes(2, 'little') + bytes([subindex])
        payload += (0 if value is None else value).to_bytes(4, 'little')
        self.send(0x601, payload)
        frame = self.wait_for(0x581, lambda f: f.data[1:4] == payload[1:4])
        if frame.data[0] == 0x80:
            raise SdoAbort(int.from_bytes(frame.data[4:8], 'little'), index)
        if value is None:
            returned_size = 4 - ((frame.data[0] >> 2) & 3)
            return int.from_bytes(frame.data[4:4 + returned_size], 'little')
        return None

    def save(self, path: Path):
        path.parent.mkdir(parents=True, exist_ok=True)
        frames = sorted(self.log, key=lambda frame: frame.timestamp)
        path.write_text('\n'.join(frame.candump() for frame in frames) + '\n', encoding='utf-8')
        path.with_suffix('.faults.json').write_text(json.dumps(self.events, indent=2) + '\n', encoding='utf-8')

    def close(self):
        process = getattr(self, 'process', None)
        if process is not None and process.poll() is None:
            try:
                process.stdin.write(b'QUIT\n')
                process.stdin.flush()
                process.communicate(timeout=3)
            except (BrokenPipeError, subprocess.TimeoutExpired):
                process.kill()
                process.communicate()
        if self.sock is not None:
            self.sock.close()
