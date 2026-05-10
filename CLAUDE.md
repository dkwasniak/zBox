# Claude Notes

This file is for agent-oriented repository notes. Public project documentation starts in [`README.md`](README.md).

## Read This First

- Use the root README for onboarding and project structure.
- Use `docs/server.md`, `docs/esp32-firmware.md`, and `docs/hardware.md` before changing a subsystem.
- Treat network addresses, hostnames, and deployment targets as installation-specific. Do not reintroduce personal LAN defaults.

## Project Shape

- `server/` contains the FastAPI backend and the web admin portal.
- `esp32/` contains the PlatformIO firmware for the NFC audio device.
- `web/` contains the static admin portal.
- `hardware/pcb/kicad/` contains the KiCad hardware sources.

## Local Skills

- For firmware work in `esp32/src/` or `esp32/test/`, load the repo-local firmware skill at `.claude/skills/firmware-dev/SKILL.md`.
- Treat `.claude/skills/firmware-dev/` as the source of truth; do not copy or move that skill when updating it.

## Operational Notes

- The ESP32 is offline-first. Music and mappings live on the SD card.
- Wi-Fi is used for sync and diagnostics, not normal playback.
- The firmware still requires a compile-time server host in `esp32/src/zbox_config.h`.
- Embedded state machine patterns and rationale: [`docs/state_machine_best_practices.md`](docs/state_machine_best_practices.md).
