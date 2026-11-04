"""Attempt vcan on the hosted runner and record an explicit, inspectable fallback."""
import json
import os
from pathlib import Path
import socket
import subprocess

Path('artifacts').mkdir(exist_ok=True)
attempts = []
for command in (['sudo', 'modprobe', 'vcan'], ['sudo', 'ip', 'link', 'add', 'vcan0', 'type', 'vcan'],
                ['sudo', 'ip', 'link', 'set', 'vcan0', 'up']):
    result = subprocess.run(command, capture_output=True, text=True)
    attempts.append({'command': ' '.join(command), 'exit': result.returncode,
                     'stdout': result.stdout.strip(), 'stderr': result.stderr.strip()})
backend, reason = 'memory', 'vcan0 could not be opened; only the in-memory bench is selected.'
try:
    with socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW) as bus:
        bus.bind(('vcan0',))
    backend, reason = 'socketcan', 'vcan0 was created and a native CAN RAW socket was opened successfully.'
except OSError as error:
    reason += ' ' + str(error)
record = {'backend': backend, 'reason': reason, 'attempts': attempts}
Path('artifacts/backend.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
Path('artifacts/backend.md').write_text('# CI bus backend\n\n' + reason + '\n', encoding='utf-8')
print(json.dumps(record, indent=2))
if os.getenv('GITHUB_STEP_SUMMARY'):
    with open(os.environ['GITHUB_STEP_SUMMARY'], 'a', encoding='utf-8') as summary:
        summary.write('### Bus selection\n\n' + reason + '\n')
