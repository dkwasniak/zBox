import pytest
from serial_harness import SerialHarness


def pytest_addoption(parser):
    parser.addoption("--port", default="/dev/cu.usbserial-10",
                     help="Serial port of DUT (ESP32)")
    parser.addoption("--tester-port", default=None,
                     help="Serial port of tester ESP32-S3")
    parser.addoption("--no-reset", action="store_true",
                     help="Skip DTR reset at fixture setup")
    parser.addoption("--run-manual", action="store_true",
                     help="Run @manual tests (require interaction)")
    parser.addoption("--run-hardware", action="store_true",
                     help="Uruchom testy @requires_hardware (JBL + figurka)")


def pytest_configure(config):
    config.addinivalue_line("markers", "manual: requires physical interaction")
    config.addinivalue_line("markers", "requires_tester: requires --tester-port")
    config.addinivalue_line("markers", "requires_hardware: requires JBL + figurine")
    config.addinivalue_line("markers", "soak: long test >60s")


def pytest_collection_modifyitems(config, items):
    skip_manual = pytest.mark.skip(reason="--run-manual not provided")
    skip_hw = pytest.mark.skip(reason="--run-hardware not provided")
    run_manual = config.getoption("--run-manual", default=False)
    run_hw = config.getoption("--run-hardware", default=False)
    for item in items:
        if "manual" in item.keywords and not run_manual:
            item.add_marker(skip_manual)
        if "requires_hardware" in item.keywords and not run_hw:
            item.add_marker(skip_hw)


@pytest.fixture(scope="session")
def serial_harness(request):
    port = request.config.getoption("--port")
    harness = SerialHarness(port)
    # Not resetting here — each test does its own flush+reset.
    # A double reset (fixture + test within 0.2s) interrupts an ongoing boot.
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
