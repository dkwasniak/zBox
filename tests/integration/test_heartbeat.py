import re
import time
import pytest

# Heartbeat i DIAG testy obserwują DZIAŁAJĄCE urządzenie — nie resetują.
# Boot testy (poprzednia suita) wykonały już reset i device jest online.
# To eliminuje problem wielokrotnych resetów BT (JBL przestaje reconnectować).
#
# Timeouty:
#   [LOOP] alive co 5s — czekamy max 10s na kolejny
#   [NFC] alive co 5s — czekamy max 10s
#   [DIAG] HWM co 30s — czekamy max 35s
#
# Jeśli device nie działa (brak heartbeatu w 10s), test FAIL — co jest poprawne.


def test_loop_heartbeat_appears(serial_harness):
    """[LOOP] alive pojawia się w ciągu 10s (device już uruchomiony)."""
    serial_harness.flush()
    # Bez reset — obserwujemy działające urządzenie
    serial_harness.wait_for_line(r"\[LOOP\] alive", timeout_s=10)


def test_loop_heartbeat_repeats_3x(serial_harness):
    """3 różne heartbeaty loopowe — każdy co ~5s."""
    serial_harness.flush()
    matches = serial_harness.wait_for_nth_occurrence(
        r"\[LOOP\] alive", n=3, timeout_s=25
    )
    assert len(matches) == 3


def test_nfc_task_heartbeat(serial_harness):
    """[NFC] alive hwm=N err=N pojawia się w ciągu 10s."""
    serial_harness.flush()
    serial_harness.wait_for_line(r"\[NFC\] alive hwm=\d+ err=\d+", timeout_s=10)


def test_diag_hwm_appears(serial_harness):
    """[DIAG] HWM pojawia się w ciągu 35s (timer co 30s)."""
    serial_harness.flush()
    serial_harness.wait_for_line(r"\[DIAG\] HWM", timeout_s=35)


def test_diag_all_tasks_above_512(serial_harness):
    """Wszystkie stack HWM > 512 bajtów — brak ryzyka stack overflow."""
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
        assert hwm > 512, f"Stack HWM dla {task!r} za mały: {hwm} B"


def test_diag_heap_appears(serial_harness):
    """[DIAG] heap free=N pojawia się w ciągu 35s."""
    serial_harness.flush()
    serial_harness.wait_for_line(r"\[DIAG\] heap free=\d+", timeout_s=35)


@pytest.mark.soak
def test_no_freeze_60s(serial_harness):
    """Brak przerwy >15s w heartbeatach przez 60s — weryfikacja braku zawieszenia."""
    serial_harness.flush()
    end_time = time.monotonic() + 60
    while time.monotonic() < end_time:
        remaining = end_time - time.monotonic()
        if remaining <= 0:
            break
        serial_harness.wait_for_line(r"\[LOOP\] alive", timeout_s=min(15, remaining))
