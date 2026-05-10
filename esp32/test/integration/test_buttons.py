import pytest


pytestmark = pytest.mark.requires_tester


def _boot_ready(harness):
    """Waits for end of boot sequence (BT A2DP starting)."""
    harness.wait_for_line(r"\[T\+\s*\d+\] BT A2DP starting", timeout_s=30)


def test_volume_up_logs(reset_esp, button_tester):
    """Short press BTN_D → [VOL] in log."""
    _boot_ready(reset_esp)
    button_tester.press("D", 100)
    reset_esp.wait_for_line(r"\[VOL\]", timeout_s=3)


def test_volume_down_logs(reset_esp, button_tester):
    """Short press BTN_C → [VOL] in log."""
    _boot_ready(reset_esp)
    button_tester.press("C", 100)
    reset_esp.wait_for_line(r"\[VOL\]", timeout_s=3)


def test_battery_long_press_a(reset_esp, button_tester):
    """Long press BTN_A (2500ms) → [BAT] Voltage: in log."""
    _boot_ready(reset_esp)
    button_tester.press("A", 2500)
    reset_esp.wait_for_line(r"\[BAT\] Voltage:", timeout_s=5)


def test_battery_voltage_range(reset_esp, button_tester):
    """Voltage from [BAT] log is within the 3.0V–4.5V range."""
    import re
    _boot_ready(reset_esp)
    button_tester.press("A", 2500)
    m = reset_esp.wait_for_line(r"\[BAT\] Voltage: ([\d.]+)V", timeout_s=5)
    voltage = float(m.group(1))
    assert 3.0 <= voltage <= 4.5, f"Voltage out of range: {voltage}V"


def test_deep_sleep_trigger(reset_esp, button_tester):
    """Long press BTN_C (2500ms) → [SLEEP] in log."""
    _boot_ready(reset_esp)
    button_tester.press("C", 2500)
    reset_esp.wait_for_line(r"\[SLEEP\]", timeout_s=5)


def test_deep_sleep_long_press_c_does_not_change_volume(reset_esp, button_tester):
    """Long BTN_C must not trigger a short [VOL] beforehand."""
    _boot_ready(reset_esp)
    reset_esp.flush()
    button_tester.press("C", 2500)
    reset_esp.wait_for_line(r">>> DEEP SLEEP", timeout_s=5)
    lines = list(reset_esp._buf)
    assert not any("[VOL]" in line for line in lines), f"Unexpected [VOL] log: {lines}"


def test_sync_mode_trigger(reset_esp, button_tester):
    """Combo BTN_C+BTN_D (2500ms) → sync flag written + restart + SYNC MODE."""
    _boot_ready(reset_esp)
    button_tester.press_combo("CD", 2500)

    # 1. Sync flag written
    reset_esp.wait_for_line(r">>> Sync flag written", timeout_s=5)
    # 2. Restart (new boot)
    reset_esp.wait_for_line(r"=== zBox ===", timeout_s=10)
    # 3. Sync mode
    reset_esp.wait_for_line(r"zBox SYNC MODE", timeout_s=10)


def test_long_press_b_switches_mode_without_click(reset_esp, button_tester):
    """Long BTN_B switches mode without emitting BTN_B clicks."""
    _boot_ready(reset_esp)
    reset_esp.flush()
    button_tester.press("B", 2500)
    reset_esp.wait_for_line(r"\[MODE\] Toggle requested by long press B", timeout_s=5)
    reset_esp.wait_for_line(r"\[MODE\] Saved mode=", timeout_s=5)
    lines = list(reset_esp._buf)
    assert not any("B single click" in line for line in lines), f"Unexpected BTN_B click: {lines}"
