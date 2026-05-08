# Random Playback Hang Notes

This file is a development note for investigating a historical runtime freeze seen during Bluetooth playback.

## Symptoms

- audio continued playing
- buttons stopped reacting
- LED animation stopped updating

## Main hypotheses

1. The LED task stack was too small and could corrupt adjacent task state.
2. Bluetooth radio activity and the LED transport could starve each other on the same core.
3. Some shared state may have needed stricter synchronization.

## Practical direction

- increase LED task stack size first
- capture diagnostic logs before and after a freeze
- compare whether the LED task, main loop, or both stop reporting heartbeat logs

This note is intentionally brief. If the issue reappears, capture fresh logs and update the analysis with current firmware behavior.
