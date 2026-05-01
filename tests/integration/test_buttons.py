import pytest


pytestmark = pytest.mark.requires_tester


def _boot_ready(harness):
    """Czeka na koniec boot sequence (BT A2DP starting)."""
    harness.wait_for_line(r"\[T\+\s*\d+\] BT A2DP starting", timeout_s=30)


def test_volume_up_logs(reset_esp, button_tester):
    """Krótkie naciśnięcie BTN_D → [VOL] w logu."""
    _boot_ready(reset_esp)
    button_tester.press("D", 100)
    reset_esp.wait_for_line(r"\[VOL\]", timeout_s=3)


def test_volume_down_logs(reset_esp, button_tester):
    """Krótkie naciśnięcie BTN_C → [VOL] w logu."""
    _boot_ready(reset_esp)
    button_tester.press("C", 100)
    reset_esp.wait_for_line(r"\[VOL\]", timeout_s=3)


def test_battery_long_press_a(reset_esp, button_tester):
    """Długie naciśnięcie BTN_A (2500ms) → [BAT] Voltage: w logu."""
    _boot_ready(reset_esp)
    button_tester.press("A", 2500)
    reset_esp.wait_for_line(r"\[BAT\] Voltage:", timeout_s=5)


def test_battery_voltage_range(reset_esp, button_tester):
    """Napięcie z [BAT] logu mieści się w zakresie 3.0V–4.5V."""
    import re
    _boot_ready(reset_esp)
    button_tester.press("A", 2500)
    m = reset_esp.wait_for_line(r"\[BAT\] Voltage: ([\d.]+)V", timeout_s=5)
    voltage = float(m.group(1))
    assert 3.0 <= voltage <= 4.5, f"Napięcie poza zakresem: {voltage}V"


def test_deep_sleep_trigger(reset_esp, button_tester):
    """Długie naciśnięcie BTN_C (2500ms) → [SLEEP] w logu."""
    _boot_ready(reset_esp)
    button_tester.press("C", 2500)
    reset_esp.wait_for_line(r"\[SLEEP\]", timeout_s=5)


def test_sync_mode_trigger(reset_esp, button_tester):
    """Combo BTN_C+BTN_D (2500ms) → Sync flag + restart + SYNC MODE."""
    _boot_ready(reset_esp)
    button_tester.press_combo("CD", 2500)

    # 1. Sync flag zapisana
    reset_esp.wait_for_line(r">>> Sync flag written", timeout_s=5)
    # 2. Restart (nowy boot)
    reset_esp.wait_for_line(r"=== MusicBox ===", timeout_s=10)
    # 3. Tryb sync
    reset_esp.wait_for_line(r"MusicBox SYNC MODE", timeout_s=10)
