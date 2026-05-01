import re
import threading
import time
from collections import deque

import serial


class SerialHarness:
    def __init__(self, port: str, baudrate: int = 115200):
        self._ser = serial.Serial(port, baudrate, timeout=0.1)
        self._buf: deque[str] = deque(maxlen=2000)
        self._lock = threading.Lock()
        self._new_line = threading.Event()
        self._running = True
        self._thread = threading.Thread(target=self._reader, daemon=True)
        self._thread.start()

    def _reader(self):
        while self._running:
            try:
                raw = self._ser.readline()
                if raw:
                    line = raw.decode("utf-8", errors="replace").rstrip()
                    with self._lock:
                        self._buf.append(line)
                    self._new_line.set()
            except serial.SerialException:
                break

    def wait_for_line(self, pattern: str, timeout_s: float) -> re.Match:
        """Block until a line matching pattern appears. Returns the Match object."""
        regex = re.compile(pattern)
        deadline = time.monotonic() + timeout_s
        seen_up_to = 0

        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f"Pattern not seen within {timeout_s}s: {pattern!r}")

            with self._lock:
                snapshot = list(self._buf)

            for line in snapshot[seen_up_to:]:
                m = regex.search(line)
                if m:
                    return m
            seen_up_to = len(snapshot)

            self._new_line.wait(timeout=min(remaining, 0.2))
            self._new_line.clear()

    def wait_for_sequence(self, patterns: list, timeout_s: float) -> list:
        """Wait for patterns to appear in order. Returns list of Match objects."""
        matches = []
        remaining = timeout_s
        for pattern in patterns:
            start = time.monotonic()
            matches.append(self.wait_for_line(pattern, remaining))
            remaining -= time.monotonic() - start
            if remaining <= 0 and pattern != patterns[-1]:
                raise TimeoutError(f"Sequence timed out after pattern: {pattern!r}")
        return matches

    def reset_device(self):
        """DTR pulse to reset ESP32 via EN pin."""
        self._ser.setDTR(False)
        time.sleep(0.1)
        self._ser.setDTR(True)
        time.sleep(0.1)

    def flush(self):
        """Clear the line buffer and event."""
        with self._lock:
            self._buf.clear()
        self._new_line.clear()

    def close(self):
        self._running = False
        self._thread.join(timeout=2)
        self._ser.close()
