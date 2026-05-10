import pytest

# Expected log order during a normal boot (without sync_pending)
BOOT_SEQUENCE = [
    r"=== zBox ===",
    r"\[T\+\s*0\] Boot start",
    r"\[T\+\s*\d+\] GPIO ready",
    r"\[T\+\s*\d+\] SD (OK|FAIL)",
    r"\[BOOT\] sync_pending flag: 0",
    r"--- Normal mode",
    r"\[T\+\s*\d+\] NFC (OK|FAIL)",
    r"\[T\+\s*\d+\] Mappings loaded",
    r"\[T\+\s*\d+\] BT A2DP starting",
]


def test_boot_sequence_order(serial_harness):
    """All boot logs appear in the correct order."""
    serial_harness.flush()
    serial_harness.reset_device()
    serial_harness.wait_for_sequence(BOOT_SEQUENCE, timeout_s=30)


def test_sd_reports_status(serial_harness):
    """SD log contains OK or FAIL."""
    serial_harness.flush()
    serial_harness.reset_device()
    serial_harness.wait_for_line(r"\[T\+\s*\d+\] SD (OK|FAIL)", timeout_s=10)


def test_nfc_reports_status(serial_harness):
    """NFC log contains OK or FAIL."""
    serial_harness.flush()
    serial_harness.reset_device()
    serial_harness.wait_for_line(r"\[T\+\s*\d+\] NFC (OK|FAIL)", timeout_s=15)


def test_mappings_count_logged(serial_harness):
    """Mappings loaded log contains the figurine count."""
    serial_harness.flush()
    serial_harness.reset_device()
    serial_harness.wait_for_line(r"Mappings loaded \(\d+\)", timeout_s=20)


def test_bt_starting_with_name(serial_harness):
    """BT A2DP starting logs the speaker name."""
    serial_harness.flush()
    serial_harness.reset_device()
    serial_harness.wait_for_line(r"BT A2DP starting -> JBL GO 2", timeout_s=20)
