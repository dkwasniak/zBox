import pytest
from serial_harness import SerialHarness


def pytest_addoption(parser):
    parser.addoption("--port", default="/dev/cu.usbserial-10",
                     help="Serial port of DUT (ESP32)")
    parser.addoption("--tester-port", default=None,
                     help="Serial port of tester ESP32-S3")
    parser.addoption("--no-reset", action="store_true",
                     help="Skip DTR reset at fixture setup")


def pytest_configure(config):
    config.addinivalue_line("markers", "manual: wymaga fizycznej interakcji")
    config.addinivalue_line("markers", "requires_tester: wymaga --tester-port")
    config.addinivalue_line("markers", "requires_hardware: wymaga JBL + figurki")
    config.addinivalue_line("markers", "soak: długi test >60s")


@pytest.fixture(scope="session")
def serial_harness(request):
    port = request.config.getoption("--port")
    harness = SerialHarness(port)
    if not request.config.getoption("--no-reset"):
        harness.reset_device()
    yield harness
    harness.close()


@pytest.fixture(scope="session")
def button_tester(request):
    from button_tester import ButtonTester
    tester_port = request.config.getoption("--tester-port")
    if tester_port is None:
        pytest.skip("--tester-port not provided")
    tester = ButtonTester(tester_port)
    yield tester
    tester.close()


@pytest.fixture
def reset_esp(serial_harness, request):
    """Reset DUT and flush buffer before each test."""
    serial_harness.flush()
    if not request.config.getoption("--no-reset"):
        serial_harness.reset_device()
    return serial_harness
