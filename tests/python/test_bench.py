import time

import pytest

from canbench.bench import SdoAbort


def operational(bench):
    started = time.monotonic()
    bench.nmt(1)
    frame = bench.wait_for(0x701, lambda f: f.data == b'\x05')
    assert time.monotonic() - started < .5
    return frame


def test_nmt_operational_deadline_and_stop(bench):
    assert bench.sdo(0x1001) == 0
    operational(bench)
    bench.nmt(2)
    assert bench.wait_for(0x701, lambda f: f.data == b'\x04').dlc == 1
    bench.nmt(128)
    bench.wait_for(0x701, lambda f: f.data == b'\x7f')


def test_sdo_range_abort_preserves_target(bench):
    bench.sdo(0x2001, 123)
    with pytest.raises(SdoAbort) as error:
        bench.sdo(0x2001, 1001)
    assert error.value.code == 0x06090030
    assert bench.sdo(0x2001) == 123


def test_sdo_readonly_abort(bench):
    with pytest.raises(SdoAbort) as error:
        bench.sdo(0x2000, 5)
    assert error.value.code == 0x06010002


def test_motion_and_tpdo_mapping(bench):
    operational(bench)
    bench.pdo(8, 1, 0)
    bench.wait_for(0x181, lambda f: f.data[2] == 1)
    bench.advance(100)
    frame = bench.wait_for(0x181, lambda f: int.from_bytes(f.data[:2], 'little') == 8 and f.data[2] == 0)
    assert frame.data[3] == 0
    assert bench.sdo(0x2000) == 8


def test_dropped_command_does_not_change_target(bench):
    operational(bench)
    bench.pdo(8, 1, 0, drop=True)
    assert bench.sdo(0x2001) == 0
    assert bench.sdo(0x2002) == 0


def test_dropped_refresh_trips_watchdog(bench):
    operational(bench)
    bench.pdo(1000, 1, 0)
    bench.wait_for(0x181, lambda f: f.data[2] == 1)
    bench.pdo(1000, 1, 1, drop=True)
    bench.advance(300)
    bench.wait_for(0x181, lambda f: f.data[2] == 0 and f.data[3] & 0x10)
    assert bench.sdo(0x2002) == 0
    assert bench.sdo(0x1001) & 0x10


def test_delayed_command_cannot_restart_faulted_motor(bench):
    operational(bench)
    bench.pdo(1000, 1, 0)
    bench.wait_for(0x181, lambda f: f.data[2] == 1)
    bench.pdo(900, 1, 1, delay_ms=300)
    bench.wait_for(0x181, lambda f: f.data[2] == 0 and f.data[3] & 0x10)
    assert bench.sdo(0x2002) == 0
    assert bench.sdo(0x2001) == 1000


def test_corrupted_payload_stops_motor_and_latches_error(bench):
    operational(bench)
    bench.pdo(1000, 1, 0)
    bench.wait_for(0x181, lambda f: f.data[2] == 1)
    bench.pdo(900, 1, 1, corrupt=True)
    bench.wait_for(0x181, lambda f: f.data[2] == 0 and f.data[3] & 1)
    assert bench.sdo(0x2001) == 1000
    assert bench.sdo(0x1001) & 1


def test_sequence_gap_fault_and_explicit_acknowledgement(bench):
    operational(bench)
    bench.pdo(1000, 1, 0)
    bench.wait_for(0x181, lambda f: f.data[2] == 1)
    bench.pdo(900, 1, 2)
    bench.wait_for(0x181, lambda f: f.data[3] & 0x10)
    bench.sdo(0x2004, 1, size=1)
    assert bench.sdo(0x1001) == 0
    bench.pdo(5, 1, 10)
    bench.wait_for(0x181, lambda f: f.data[2] == 1 and f.data[3] == 0)


def test_preop_ignores_motion_and_produces_heartbeat(bench):
    bench.pdo(20, 1, 0)
    assert bench.sdo(0x2001) == 0
    bench.advance(110)
    bench.wait_for(0x701, lambda f: f.data == b'\x7f')


def test_motor_start_in_preop_is_rejected(bench):
    with pytest.raises(SdoAbort) as error:
        bench.sdo(0x2002, 1, size=1)
    assert error.value.code == 0x08000022


def test_broadcast_reset_restores_defaults(bench):
    bench.sdo(0x2001, 55)
    bench.nmt(129, target=0)
    bench.wait_for(0x701, lambda f: f.data == b'\x00')
    assert bench.sdo(0x2001) == 0
