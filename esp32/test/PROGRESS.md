# zBox Test Progress

| Stage | Description | Status | Commit |
|------|------|--------|--------|
| 1 | Native unit tests | [x] | fecb3c1 |
| 2 | Serial harness + boot/timing | [x] | 57c183a |
| 3 | Heartbeat + soak | [x] | 57c183a |
| 4 | Tester ESP32-S3 firmware | [ ] | 324694c |
| 5 | Button automation tests | [ ] | 125b2da |
| 6 | Manual NFC tests | [ ] | 90de07f |

## Legend
- `[x]` = run and passed on hardware
- `[ ]` = code ready, waiting to be run

## Results

### Stage 1 — native (without hardware)
```
native:test_uid_format  7/7   PASS
native:test_mapping     8/8   PASS
native:test_volume      16/16 PASS
```

### Stages 2+3 — DUT via /dev/cu.usbserial-10
```
test_boot_sequence_order       PASS
test_sd_reports_status         PASS
test_nfc_reports_status        PASS
test_mappings_count_logged     PASS
test_bt_starting_with_name     PASS
test_gpio_ready_within_200ms   PASS  (T+5ms)
test_sd_init_within_500ms      PASS  (T+24ms)
test_nfc_init_within_2500ms    PASS  (T+1576ms)
test_bt_start_within_5000ms    PASS  (T+3638ms)
test_loop_heartbeat_appears    PASS
test_loop_heartbeat_repeats_3x PASS
test_nfc_task_heartbeat        PASS
test_diag_hwm_appears          PASS
test_diag_all_tasks_above_512  PASS  (check values in logs)
test_diag_heap_appears         PASS
```

## Running
```bash
# Stage 1 (without hardware)
cd esp32 && pio test -e native -v

# Stages 2+3
cd esp32/test
pytest integration/test_boot_sequence.py integration/test_timing.py \
  integration/test_heartbeat.py -v --port /dev/cu.usbserial-10 -m "not soak"

# Stage 3 soak (long)
pytest integration/test_heartbeat.py -v --port /dev/cu.usbserial-10 -m soak

# Stages 5+6 (requires tester ESP32-S3)
pytest integration/test_buttons.py -v \
  --port /dev/cu.usbserial-10 --tester-port /dev/cu.usbmodem<N>
pytest integration/test_nfc_playback.py -v --port /dev/cu.usbserial-10 --run-manual
```
