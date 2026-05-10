import re
import time
import pytest

# Heartbeat and DIAG tests observe a RUNNING device — they do not reset it.
# Boot tests (previous suite) already performed a reset and the device is online.
# This eliminates the problem of repeated BT resets (JBL stops reconnecting).
#
# Timeouts:
#   [LOOP] alive every 5s — wait max 10s for the next one
#   [NFC] alive every 5s — wait max 10s
#   [DIAG] HWM every 30s — wait max 35s
#
# If the device is not running (no heartbeat within 10s), test FAIL — which is correct.


def test_loop_heartbeat_appears(serial_harness):
    """[LOOP] alive appears within 10s (device already running)."""
    serial_harness.flush()
    # No reset — observing a running device
    serial_harness.wait_for_line(r"\[LOOP\] alive", timeout_s=10)


def test_loop_heartbeat_repeats_3x(serial_harness):
    """3 distinct loop heartbeats — each ~5s apart."""
    serial_harness.flush()
    matches = serial_harness.wait_for_nth_occurrence(
        r"\[LOOP\] alive", n=3, timeout_s=25
    )
    assert len(matches) == 3


def test_nfc_task_heartbeat(serial_harness):
    """[NFC] alive hwm=N err=N appears within 10s."""
    serial_harness.flush()
    serial_harness.wait_for_line(r"\[NFC\] alive hwm=\d+ err=\d+", timeout_s=10)


def test_diag_hwm_appears(serial_harness):
    """[DIAG] HWM appears within 35s (timer every 30s)."""
    serial_harness.flush()
    serial_harness.wait_for_line(r"\[DIAG\] HWM", timeout_s=35)


def test_diag_all_tasks_above_512(serial_harness):
    """All stack HWMs > 512 bytes — no risk of stack overflow."""
    serial_harness.flush()
    m = serial_harness.wait_for_line(
        r"\[DIAG\] HWM loop=(\d+) led=(\d+) audio=(\d+) nfc=(\d+)",
        timeout_s=35,
    )
    values = {
        "loop":  int(m.group(1)),
        "led":   int(m.group(2)),
        "audio": int(m.group(3)),
        "nfc":   int(m.group(4)),
    }
    for task, hwm in values.items():
        assert hwm > 512, f"Stack HWM for {task!r} too small: {hwm} B"


def test_diag_heap_appears(serial_harness):
    """[DIAG] heap free=N appears within 35s."""
    serial_harness.flush()
    serial_harness.wait_for_line(r"\[DIAG\] heap free=\d+", timeout_s=35)


@pytest.mark.soak
def test_no_freeze_60s(serial_harness):
    """No gap >15s between heartbeats over 60s — verifies absence of freeze."""
    serial_harness.flush()
    end_time = time.monotonic() + 60
    while time.monotonic() < end_time:
        remaining = end_time - time.monotonic()
        if remaining <= 0:
            break
        serial_harness.wait_for_line(r"\[LOOP\] alive", timeout_s=min(15, remaining))
