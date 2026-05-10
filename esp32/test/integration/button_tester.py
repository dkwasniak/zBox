import serial
import time


class ButtonTester:
    """Communication with the tester ESP32-S3 over serial."""

    def __init__(self, port: str, baudrate: int = 115200):
        self._ser = serial.Serial(port, baudrate, timeout=5)
        time.sleep(0.5)  # wait for reset after opening port
        # Flush input buffer
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
        """Press one button (A/B/C/D) for duration_ms milliseconds."""
        timeout = (duration_ms / 1000) + 3
        self._ser.timeout = timeout
        self._send(f"PRESS {btn.upper()} {duration_ms}")
        self._ser.timeout = 5

    def press_combo(self, btns: str, duration_ms: int):
        """Press two buttons simultaneously (e.g. 'CD') for duration_ms ms."""
        timeout = (duration_ms / 1000) + 3
        self._ser.timeout = timeout
        self._send(f"PRESS_COMBO {btns.upper()} {duration_ms}")
        self._ser.timeout = 5

    def double_press(self, btn: str, duration_ms: int = 100, gap_ms: int = 120):
        """Two short clicks of the same button."""
        self.press(btn, duration_ms)
        time.sleep(gap_ms / 1000)
        self.press(btn, duration_ms)

    def release_all(self):
        """Release all buttons (Hi-Z)."""
        self._send("RELEASE ALL")

    def close(self):
        self.release_all()
        self._ser.close()
