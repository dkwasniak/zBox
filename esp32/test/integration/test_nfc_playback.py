import re
import pytest


pytestmark = pytest.mark.manual


@pytest.mark.manual
def test_nfc_place_triggers_playback(serial_harness):
    """Place figurine on NFC pad → startPlayback + PLAYBACK START."""
    input("\n[MANUAL] Place the figurine on the NFC pad, then press Enter...")
    serial_harness.wait_for_line(r"\[PLAY\] startPlayback uid=", timeout_s=5)
    serial_harness.wait_for_line(r">>> PLAYBACK START", timeout_s=5)


@pytest.mark.manual
def test_uid_format_valid(serial_harness):
    """UID logged by firmware is in XX:XX:XX:XX format (hex, uppercase)."""
    input("\n[MANUAL] Place the figurine on the NFC pad, then press Enter...")
    m = serial_harness.wait_for_line(
        r"\[PLAY\] startPlayback uid=([0-9A-F]{2}(?::[0-9A-F]{2}){3,6})",
        timeout_s=5,
    )
    uid = m.group(1)
    assert re.fullmatch(r"[0-9A-F]{2}(:[0-9A-F]{2}){3,6}", uid), \
        f"Invalid UID format: {uid!r}"


@pytest.mark.manual
def test_nfc_remove_stops_playback(serial_harness):
    """Remove figurine from NFC pad → stopPlayback in log within 5s."""
    input("\n[MANUAL] Make sure the figurine is on the pad and music is playing."
          " Then press Enter and IMMEDIATELY remove the figurine.")
    serial_harness.wait_for_line(r"\[STOP\] stopPlayback", timeout_s=5)


@pytest.mark.manual
def test_audio_telemetry(serial_harness):
    """During playback the [AUDIO_TEL] SD=N B/s telemetry line appears."""
    input("\n[MANUAL] Place the figurine on the NFC pad and wait until music is playing."
          " Then press Enter...")
    serial_harness.wait_for_line(
        r"\[AUDIO_TEL\] SD=\d+ B/s",
        timeout_s=10,
    )
