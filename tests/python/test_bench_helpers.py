from pathlib import Path

import pytest

from canbench.bench import NodeBench


def test_bench_bad_backend():
    with pytest.raises(ValueError, match='Unknown bus'):
        NodeBench(Path('build/can_node'), 'unknown')


def test_memory_timeout_validation_and_trace_output(tmp_path):
    node = NodeBench(Path('build/can_node'))
    try:
        with pytest.raises(TimeoutError, match='No matching CAN frame'):
            node.wait_for(0x555, timeout=.03)
        with pytest.raises(ValueError, match='backwards'):
            node.advance(-1)
        with pytest.raises(ValueError, match='empty'):
            node.send(1, b'', corrupt=True)
        with pytest.raises(ValueError, match='Expedited size'):
            node.sdo(0x1000, size=3)
        assert node._exchange('bad command') == -1
        assert node._exchange('F 800 0') == -2
        assert node._exchange('F 001 9 000000000000000000') == -3
        node.save(tmp_path / 'trace.log')
        assert '(0.000000) memory 701#00' in (tmp_path / 'trace.log').read_text()
    finally:
        node.close()


def test_host_cli_invalid_backend_and_interface():
    import subprocess
    invalid = subprocess.run(['build/can_node', '--invalid'], capture_output=True, text=True)
    assert invalid.returncode == 2 and 'Usage:' in invalid.stderr
    invalid = subprocess.run(['build/can_node', '--socketcan', 'missing-interface-too-long'], capture_output=True, text=True)
    assert invalid.returncode == 1 and 'Cannot open' in invalid.stderr


def test_memory_parser_rejects_invalid_and_overflowing_tokens():
    node = NodeBench(Path('build/can_node'))
    try:
        for command in ('T nope', 'T -1', 'T 4294967296', 'T 1 trailing',
                        'F 001 1 GG', 'F 001 1 1G', 'F 001 1 00 extra', 'F 001 4294967296'):
            assert node._exchange(command) < 0, command
        assert node._exchange('F 123 0') == 0
    finally:
        node.close()
