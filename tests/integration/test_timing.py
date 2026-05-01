import re
import pytest


def extract_t_ms(text: str) -> int:
    """Wyciąga wartość T+ (ms) z łańcucha zawierającego [T+N]."""
    m = re.search(r'\[T\+\s*(\d+)\]', text)
    if not m:
        raise ValueError(f"No [T+N] in text: {text!r}")
    return int(m.group(1))


def _boot_and_wait(serial_harness, pattern, timeout_s):
    serial_harness.flush()
    serial_harness.reset_device()
    serial_harness.wait_for_line(r"\[T\+\s*0\] Boot start", timeout_s=10)
    m = serial_harness.wait_for_line(pattern, timeout_s=timeout_s)
    return extract_t_ms(m.group(0))


def test_gpio_ready_within_200ms(serial_harness):
    t = _boot_and_wait(serial_harness, r"\[T\+\s*\d+\] GPIO ready", timeout_s=5)
    assert t <= 200, f"GPIO ready za późno: {t}ms"


def test_sd_init_within_500ms(serial_harness):
    t = _boot_and_wait(serial_harness, r"\[T\+\s*\d+\] SD (OK|FAIL)", timeout_s=10)
    assert t <= 500, f"SD init za późno: {t}ms"


def test_nfc_init_within_2500ms(serial_harness):
    t = _boot_and_wait(serial_harness, r"\[T\+\s*\d+\] NFC (OK|FAIL)", timeout_s=15)
    assert t <= 2500, f"NFC init za późno: {t}ms"


def test_bt_start_within_3000ms(serial_harness):
    t = _boot_and_wait(serial_harness, r"\[T\+\s*\d+\] BT A2DP starting", timeout_s=20)
    assert t <= 3000, f"BT start za późno: {t}ms"


@pytest.mark.requires_hardware
def test_boot_to_play_under_10s(serial_harness):
    """Wymaga podłączonego JBL + figurki na padzie przed bootem."""
    serial_harness.flush()
    serial_harness.reset_device()
    # [T+0] Boot start → PLAYBACK START musi zmieścić się w 10s od resetu
    serial_harness.wait_for_line(r"\[T\+\s*0\] Boot start", timeout_s=10)
    serial_harness.wait_for_line(r">>> PLAYBACK START", timeout_s=10)
