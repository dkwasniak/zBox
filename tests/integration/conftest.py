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
                     help="Uruchom testy @manual (wymagają interakcji)")
    parser.addoption("--run-hardware", action="store_true",
                     help="Uruchom testy @requires_hardware (JBL + figurka)")


def pytest_configure(config):
    config.addinivalue_line("markers", "manual: wymaga fizycznej interakcji")
    config.addinivalue_line("markers", "requires_tester: wymaga --tester-port")
    config.addinivalue_line("markers", "requires_hardware: wymaga JBL + figurki")
    config.addinivalue_line("markers", "soak: długi test >60s")


def pytest_collection_modifyitems(config, items):
    skip_manual = pytest.mark.skip(reason="--run-manual nie podano")
    skip_hw = pytest.mark.skip(reason="--run-hardware nie podano")
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
    # Nie resetujemy tu — każdy test robi flush+reset sam.
    # Podwójny reset (fixture + test w 0.2s) przerywa trwający boot.
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
