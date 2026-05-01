import re
import time
import pytest


def test_loop_heartbeat_appears(serial_harness):
    """[LOOP] alive pojawia się w ciągu 10s od boot."""
    serial_harness.flush()
    serial_harness.reset_device()
    serial_harness.wait_for_line(r"\[LOOP\] alive", timeout_s=10)


def test_loop_heartbeat_repeats_3x(serial_harness):
    """3 heartbeaty loopowe z interwałem ≤ 8s każdy."""
    serial_harness.flush()
    serial_harness.reset_device()
    # Czekaj na pierwszy heartbeat po normalnym boot
    serial_harness.wait_for_line(r"--- Normal mode", timeout_s=30)

    timestamps = []
    for _ in range(3):
        serial_harness.wait_for_line(r"\[LOOP\] alive", timeout_s=10)
        timestamps.append(time.monotonic())

    for i in range(1, len(timestamps)):
        interval = timestamps[i] - timestamps[i - 1]
        assert interval <= 8, f"Heartbeat {i} za późno: {interval:.1f}s"


def test_nfc_task_heartbeat(serial_harness):
    """[NFC] alive hwm=N err=N pojawia się w ciągu 10s."""
    serial_harness.flush()
    serial_harness.reset_device()
    serial_harness.wait_for_line(r"\[NFC\] alive hwm=\d+ err=\d+", timeout_s=10)


def test_diag_hwm_appears(serial_harness):
    """[DIAG] HWM pojawia się w ciągu 40s."""
    serial_harness.flush()
    serial_harness.reset_device()
    serial_harness.wait_for_line(r"\[DIAG\] HWM", timeout_s=40)


def test_diag_all_tasks_above_512(serial_harness):
    """Wszystkie stack HWM > 512 bajtów — brak ryzyka stack overflow."""
    serial_harness.flush()
    serial_harness.reset_device()
    m = serial_harness.wait_for_line(
        r"\[DIAG\] HWM loop=(\d+) led=(\d+) audio=(\d+) nfc=(\d+)",
        timeout_s=40,
    )
    values = {
        "loop": int(m.group(1)),
        "led":  int(m.group(2)),
        "audio": int(m.group(3)),
        "nfc":  int(m.group(4)),
    }
    for task, hwm in values.items():
        assert hwm > 512, f"Stack HWM dla {task!r} za mały: {hwm} B"


def test_diag_heap_appears(serial_harness):
    """[DIAG] heap free=N pojawia się w ciągu 40s."""
    serial_harness.flush()
    serial_harness.reset_device()
    serial_harness.wait_for_line(r"\[DIAG\] heap free=\d+", timeout_s=40)


@pytest.mark.soak
def test_no_freeze_60s(serial_harness):
    """Brak przerwy >15s w heartbeatach przez 60s — weryfikacja braku zawieszenia."""
    serial_harness.flush()
    serial_harness.reset_device()
    # Poczekaj na normalny boot
    serial_harness.wait_for_line(r"--- Normal mode", timeout_s=30)

    end_time = time.monotonic() + 60
    while time.monotonic() < end_time:
        remaining = end_time - time.monotonic()
        if remaining <= 0:
            break
        # Każdy heartbeat musi pojawić się w ciągu 15s
        serial_harness.wait_for_line(r"\[LOOP\] alive", timeout_s=min(15, remaining))
