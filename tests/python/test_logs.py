from pathlib import Path

import pytest

from canbench.logs import Frame, parse_line, read_log, statistics_report


@pytest.mark.parametrize('line,payload', [('(1.234) vcan0 181#01020304', b'\x01\x02\x03\x04'),
                                       ('(1.234) vcan0 181 [4] 01 02 03 04', b'\x01\x02\x03\x04'),
                                       ('(1) can0 000#', b'')])
def test_compact_and_bracket_formats(line, payload):
    frame = parse_line(line)
    assert frame.data == payload
    assert parse_line(frame.candump()) == frame


@pytest.mark.parametrize('payload,dlc', [('R', 0), ('R8', 8), ('R3', 3)])
def test_remote_frames_roundtrip(payload, dlc):
    frame = parse_line(f'(0.0) vcan0 123#{payload}')
    assert frame.remote and frame.dlc == dlc and not frame.data
    assert parse_line(frame.candump()) == frame


def test_error_frame_flags_and_payload():
    frame = parse_line('(0.0) vcan0 20000004#0000000000000000')
    assert frame.error and frame.can_id == 4
    assert frame.candump().endswith('20000004#0000000000000000')


@pytest.mark.parametrize('line', ['broken', '(0) can0 123#1', '(0) can0 123#GG', '(0) can0 123##0011',
                                '(0) can0 123 [2] 01', '(0) can0 800#00', '(0) can0 80000123#00',
                                '(0) can0 123#000000000000000000', '(0) can0 123#R9'])
def test_bad_frames_are_rejected(line):
    with pytest.raises(ValueError):
        parse_line(line)


@pytest.mark.parametrize('changes', [{'timestamp': float('nan')}, {'timestamp': -1}, {'can_id': -1},
                                    {'remote': True, 'data': b'1'}, {'requested_dlc': 9}])
def test_frame_invariants(changes):
    fields = {'timestamp': 0, 'channel': 'test', 'can_id': 1, 'data': b''}
    with pytest.raises(ValueError):
        Frame(**{**fields, **changes})


def test_log_comments_and_contextual_errors(tmp_path):
    path = tmp_path / 'trace.log'
    path.write_text('# comment\n\n(0) vcan0 123#00\n(0.1) vcan0 123#01\n')
    assert len(read_log(path)) == 2
    path.write_text('(1) vcan0 123#00\n(0) vcan0 123#00\n')
    with pytest.raises(ValueError, match=r'trace.log:2:.*nondecreasing'):
        read_log(path)


def test_rate_jitter_and_nominal_load_are_computed_from_intervals():
    frames = [Frame(t, 'vcan0', 0x701, b'\x05') for t in [0, .1, .21]]
    result = statistics_report(frames, bitrate=500000)
    item = result['by_id']['701']
    assert item['count'] == 3
    assert item['rate_hz'] == pytest.approx(3 / .21)
    assert item['mean_interval_ms'] == pytest.approx(105)
    assert item['jitter_stddev_ms'] == pytest.approx(5)
    assert result['estimated_load_percent'] == pytest.approx(100 * 3 * 55 / 500000 / .21)
    assert result['estimated_load_upper_percent'] > result['estimated_load_percent']


def test_fault_capture_statistics():
    result = statistics_report(read_log(Path('examples/faults.log')))
    assert result['frames'] == 7 and result['error_frames'] == 1
    assert result['estimated_load_percent'] == pytest.approx(.0964)
    assert result['by_id']['181']['count'] == 4


def test_zero_duration_empty_and_rtr_statistics():
    assert statistics_report([])['estimated_load_percent'] is None
    frames = [parse_line('(0) vcan0 123#R8'), parse_line('(1) vcan0 123#R8')]
    result = statistics_report(frames)
    assert result['remote_frames'] == 2
    assert result['estimated_load_percent'] == pytest.approx(100 * 94 / 500000)


def test_statistics_reject_bad_bitrate_order_and_multiple_channels():
    with pytest.raises(ValueError, match='Bitrate'):
        statistics_report([], 0)
    a, b = Frame(1, 'a', 1, b''), Frame(0, 'a', 1, b'')
    with pytest.raises(ValueError, match='nondecreasing'):
        statistics_report([a, b])
    with pytest.raises(ValueError, match='one CAN channel'):
        statistics_report([b, Frame(1, 'b', 1, b'')])
