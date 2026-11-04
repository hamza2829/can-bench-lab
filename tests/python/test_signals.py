from pathlib import Path

import pytest
import yaml

from canbench.logs import Frame
from canbench.signals import Signal, decode_frame, load_description


def test_lifter_decode_and_scaling():
    schema = load_description(Path('examples/lifter.yaml'))
    assert decode_frame(Frame(0, 'test', 0x181, b'\xe8\x03\x01\x10'), schema) == {'position': 1000, 'motor': 1, 'error_register': 16}
    assert Signal('temperature', 4, 8, signed=True, scale=.5, offset=10).decode(bytes.fromhex('E00F')) == 9
    assert Signal('value', 0, 8).decode(b'\xff') == 255


def test_unknown_error_and_remote_frames_are_not_signal_data():
    schema = load_description(Path('examples/lifter.yaml'))
    for frame in [Frame(0, 'test', 1, b''), Frame(0, 'test', 1, b'', error=True), Frame(0, 'test', 0x181, b'', remote=True)]:
        assert decode_frame(frame, schema) == {}


@pytest.mark.parametrize('fields', [{'name': ''}, {'start': -1}, {'length': 0}, {'length': 65}, {'start': 63, 'length': 2},
                                 {'start': 1.5}, {'scale': float('nan')}, {'offset': float('inf')},
                                 {'minimum': 5, 'maximum': 4}, {'signed': 'false'}, {'name': 1}, {'unit': None}, {'scale': '1'}])
def test_bad_signal_definitions(fields):
    with pytest.raises(ValueError):
        Signal(**{'name': 'test', 'start': 0, 'length': 8, **fields})


def test_short_payload_is_clear():
    with pytest.raises(ValueError, match='insufficient payload'):
        Signal('position', 0, 16).decode(b'\x00')


@pytest.mark.parametrize('data', [None, {'unknown': 1}, {'messages': [{'id': 0x800, 'signals': []}]},
                               {'messages': [{'id': 1, 'signals': [{'name': 'x', 'start': 0, 'length': 8, 'endian': 'big'}]}]},
                               {'messages': [{'id': 1, 'unknown': True, 'signals': []}]},
                               {'messages': [{'id': 1, 'signals': []}, {'id': 1, 'signals': []}]},
                               {'messages': [{'id': 1, 'signals': [{'name': 'x', 'start': 0, 'length': 8}, {'name': 'x', 'start': 8, 'length': 8}]}]},
                               {'heartbeats': [{'id': 1, 'timeout_ms': 0}]},
                               {'heartbeats': [{'id': 1, 'timeout_ms': 1}, {'id': 1, 'timeout_ms': 1}]}])
def test_schema_errors_include_filename(tmp_path, data):
    path = tmp_path / 'bad.yaml'
    path.write_text(yaml.safe_dump(data))
    with pytest.raises(ValueError, match='bad.yaml'):
        load_description(path)
