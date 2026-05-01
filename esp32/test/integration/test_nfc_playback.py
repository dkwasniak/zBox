import re
import pytest


pytestmark = pytest.mark.manual


@pytest.mark.manual
def test_nfc_place_triggers_playback(serial_harness):
    """Postaw figurkę na padzie NFC → startPlayback + PLAYBACK START."""
    input("\n[MANUAL] Postaw figurkę na padzie NFC, a następnie naciśnij Enter...")
    serial_harness.wait_for_line(r"\[PLAY\] startPlayback uid=", timeout_s=5)
    serial_harness.wait_for_line(r">>> PLAYBACK START", timeout_s=5)


@pytest.mark.manual
def test_uid_format_valid(serial_harness):
    """UID zalogowany przez firmware jest w formacie XX:XX:XX:XX (hex, uppercase)."""
    input("\n[MANUAL] Postaw figurkę na padzie NFC, a następnie naciśnij Enter...")
    m = serial_harness.wait_for_line(
        r"\[PLAY\] startPlayback uid=([0-9A-F]{2}(?::[0-9A-F]{2}){3,6})",
        timeout_s=5,
    )
    uid = m.group(1)
    assert re.fullmatch(r"[0-9A-F]{2}(:[0-9A-F]{2}){3,6}", uid), \
        f"Nieprawidłowy format UID: {uid!r}"


@pytest.mark.manual
def test_nfc_remove_stops_playback(serial_harness):
    """Zabierz figurkę z padu NFC → stopPlayback w logu w ciągu 5s."""
    input("\n[MANUAL] Upewnij się że figurka stoi na padzie i gra muzyka."
          " Następnie naciśnij Enter, po czym NATYCHMIAST zabierz figurkę.")
    serial_harness.wait_for_line(r"\[STOP\] stopPlayback", timeout_s=5)


@pytest.mark.manual
def test_audio_telemetry(serial_harness):
    """Podczas playback pojawia się telemetria [AUDIO_TEL] SD=N B/s."""
    input("\n[MANUAL] Postaw figurkę na padzie NFC i poczekaj aż muzyka gra."
          " Następnie naciśnij Enter...")
    serial_harness.wait_for_line(
        r"\[AUDIO_TEL\] SD=\d+ B/s",
        timeout_s=10,
    )
