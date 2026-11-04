from pathlib import Path

import pytest

from canbench.analysis import diagnose, plot_signal, write_analysis, write_report
from canbench.cli import main
from canbench.logs import Frame, read_log
from canbench.signals import load_description


def test_fault_capture_reports_all_expected_problems(tmp_path):
    frames = read_log(Path('examples/faults.log'))
    result = write_analysis(frames, load_description(Path('examples/lifter.yaml')), tmp_path)
    assert len(result['findings']) == 4
    text = (tmp_path / 'diagnosis.md').read_text()
    for fragment in ['Missing heartbeat 0x701', 'SDO abort 0x06090030', 'position out of range: 2000', 'CAN error frame']:
        assert fragment in text
    assert (tmp_path / 'signal.png').read_bytes().startswith(b'\x89PNG')
    assert (tmp_path / 'statistics.json').is_file()


def test_healthy_capture_and_single_frame_report(tmp_path):
    schema = load_description(Path('examples/lifter.yaml'))
    frames = [Frame(t, 'test', 0x701, b'\x7f') for t in [0, .1, .2]]
    result = diagnose(frames, schema)
    assert not result['findings']
    write_report(result, tmp_path / 'ok.md')
    assert 'No configured problem' in (tmp_path / 'ok.md').read_text()
    write_report(diagnose(frames[:1], schema), tmp_path / 'one.md')
    assert 'zero-duration' in (tmp_path / 'one.md').read_text()
    assert 'n/a' in (tmp_path / 'one.md').read_text()


def test_empty_capture_does_not_invent_missing_heartbeats():
    result = diagnose([], load_description(Path('examples/lifter.yaml')))
    assert not result['findings']


def test_missing_entire_heartbeat_stream_is_detected():
    frames = [Frame(t, 'test', 0x181, b'\x00\x00\x00\x00') for t in [0, 1]]
    assert 'Missing heartbeat' in diagnose(frames, load_description(Path('examples/lifter.yaml')))['findings'][0]


def test_bad_payload_and_unknown_abort_are_explained(tmp_path):
    frames = [Frame(0, 'test', 0x181, b'\x00'),
              Frame(.1, 'test', 0x581, bytes.fromhex('80012000FFFFFFFF')),
              Frame(.2, 'test', 0x181, b'\x01\x00\x00\x00'), Frame(.3, 'test', 0x181, b'', remote=True)]
    schema = load_description(Path('examples/lifter.yaml'))
    result = diagnose(frames, schema)
    assert any('malformed message' in f for f in result['findings'])
    assert any('unknown abort code' in f for f in result['findings'])
    plot_signal(frames, schema, 'position', tmp_path / 'plot.png')


def test_missing_plot_signal_is_clear(tmp_path):
    with pytest.raises(ValueError, match='No samples'):
        plot_signal([], load_description(Path('examples/lifter.yaml')), 'missing', tmp_path / 'plot.png')


def test_cli_end_to_end(tmp_path, capsys):
    assert main(['examples/faults.log', '--output', str(tmp_path)]) == 0
    assert '4 findings' in capsys.readouterr().out
    assert (tmp_path / 'diagnosis.md').is_file()


def test_cli_error_has_no_traceback(tmp_path, capsys):
    assert main([str(tmp_path / 'missing.log'), '--output', str(tmp_path)]) == 1
    error = capsys.readouterr().err
    assert 'ERROR:' in error and 'Traceback' not in error


def test_cli_invalid_bitrate(tmp_path, capsys):
    assert main(['examples/faults.log', '--output', str(tmp_path), '--bitrate', '0']) == 1
    assert 'Bitrate' in capsys.readouterr().err
