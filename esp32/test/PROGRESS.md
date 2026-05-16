# zBox Test Progress

## Current policy

The repository maintains host-side native tests only.

Removed:

- serial integration tests
- tester-firmware button automation
- manual NFC pytest flows

## Current suites

### `native`

- `test_uid_format`
- `test_mapping`
- `test_volume`
- `test_reducer`
- `test_bt_adapter`
- `test_audio_adapter`
- `test_dispatcher`

### `native_btndec`

- `test_button_decoder`

## Run

```bash
cd esp32
pio test -e native -v
pio test -e native_btndec -v
```

## Last known verification target

- host-side native suites should be green
- firmware should still build with `pio run -e lolin_d32_pro`
