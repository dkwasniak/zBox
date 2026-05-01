import serial
import time


class ButtonTester:
    """Komunikacja z tester ESP32-S3 przez serial."""

    def __init__(self, port: str, baudrate: int = 115200):
        self._ser = serial.Serial(port, baudrate, timeout=5)
        time.sleep(0.5)  # czekaj na reset po otwarciu portu
        # Opróżnij bufor wejściowy
        self._ser.reset_input_buffer()

    def _send(self, cmd: str) -> str:
        self._ser.write((cmd + "\n").encode())
        resp = self._ser.readline().decode("utf-8", errors="replace").strip()
        if resp.startswith("ERR"):
            raise RuntimeError(f"Tester error: {resp}")
        return resp

    def ping(self) -> bool:
        resp = self._send("PING")
        return resp == "PONG"

    def press(self, btn: str, duration_ms: int):
        """Wciśnij jeden przycisk (A/B/C/D) na duration_ms milisekund."""
        timeout = (duration_ms / 1000) + 3
        self._ser.timeout = timeout
        self._send(f"PRESS {btn.upper()} {duration_ms}")
        self._ser.timeout = 5

    def press_combo(self, btns: str, duration_ms: int):
        """Wciśnij dwa przyciski jednocześnie (np. 'CD') na duration_ms ms."""
        timeout = (duration_ms / 1000) + 3
        self._ser.timeout = timeout
        self._send(f"PRESS_COMBO {btns.upper()} {duration_ms}")
        self._ser.timeout = 5

    def release_all(self):
        """Zwolnij wszystkie przyciski (Hi-Z)."""
        self._send("RELEASE ALL")

    def close(self):
        self.release_all()
        self._ser.close()
