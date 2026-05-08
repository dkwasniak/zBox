# zBox

![zBox hero](assets/photos/zbox-hero.jpg)

zBox is a custom offline NFC audio box I built for my daughter. It combines an ESP32, local SD card playback, a Raspberry Pi-hosted admin portal, LED effects, and a 3D-printed enclosure into a durable player that works with NFC cards and figurines.

This repository is the full build source for the project: firmware, backend, web admin, PCB design files, and the project documentation needed to rebuild it or adapt it for a similar box. The device plays audio stored on its SD card after an NFC tag or figurine is presented, and Wi-Fi is only used for sync and maintenance.

The box itself is built around a 3D-printed shell and custom electronics. This repository focuses on the complete hardware and software stack used in the build, from the ESP32 firmware and server to the PCB sources and operating notes. Mechanical exports such as the Fusion 360 STEP model live in `hardware/cad/fusion360/step/`.

## Project gallery

| Hero | Light Mode |
| --- | --- |
| ![zBox front view](assets/photos/zbox-hero.jpg) | ![zBox in Light Mode](assets/photos/zbox-light-mode.jpg) |

| Front open | Internals | PCB |
| --- | --- | --- |
| ![zBox front opened](assets/photos/zbox-front-open.jpg) | ![zBox internal layout](assets/photos/zbox-internals.jpg) | ![zBox PCB](assets/photos/zbox-pcb.jpg) |

## Repository layout

```text
.
├── docs/                   Project documentation
├── esp32/                  ESP32 firmware (PlatformIO)
├── server/                 FastAPI backend and web admin UI
├── web/                    Static admin portal
├── hardware/               PCB, KiCad, Gerbers, and CAD exports
├── assets/                 Project photos and media used in docs
├── scripts/                Utility scripts
├── Dockerfile
└── docker-compose.yml
```

## Project split

- `esp32/` contains the offline playback firmware, Bluetooth audio pipeline, NFC handling, LED logic, and sync mode.
- `server/` contains the FastAPI backend, SQLite integration, sync logic, and admin API.
- `web/` contains the static admin portal served by the backend.
- `hardware/pcb/kicad/` contains the PCB and schematic sources for the hardware build.
- `hardware/pcb/gerbers/` contains manufacturing outputs.
- `hardware/cad/fusion360/step/` contains exported mechanical CAD files such as STEP models.
- `assets/photos/` contains the product images used in this README.

## What to buy

If you want to build the current hardware revision, this is the practical shopping list. The various `PAD_*` items in KiCad are just solder pads for wires, not parts you need to buy.

- `1x` ESP32 Lolin D32 Pro
- `1x` PN532 NFC module that supports SPI mode
- `1x` MT3608 step-up module
- `1x` USB-C receptacle `GCT USB4085` or a compatible `USB 2.0 14-pin` footprint match
- `1x` P-channel MOSFET `AO3415A` in `SOT-23`
- `1x` NPN transistor `BC547` in `TO-92`
- `1x` electrolytic capacitor `470uF`
- `2x` resistors `5.1k`
- `1x` resistor `2.2k`
- `1x` resistor `10k`
- `1x` resistor `22k`
- `1x` resistor `100k`
- `1x` WS2812B LED strip or panel for the front light matrix
- `4x` momentary tactile buttons for `A`, `B`, `C`, and `D`
- `1x` LiPo battery, currently `Akyga LP805080 3.7V / 4000mAh` with `JST 2-pin` connector
  This is the battery used in my build. Pack size: `8 mm` thick, `50 mm` wide, `80 mm` high. If you swap it for another pack, check the physical dimensions against the enclosure first.
- `1x` Bluetooth speaker, currently `JBL Go 2`
- hookup wire for all off-board connections

You will also need the non-electronic build parts:

- 3D-printed enclosure parts from the project CAD
- speaker holder inserts matching the audio setup you choose
- screws or other mounting hardware that fit your chosen assembly method

## Hardware overview

```text
NFC card / figurine
        │
        ▼
   PN532 reader
        │
        ▼
 ESP32 Lolin D32 Pro
   ├── SD card with local audio
   ├── WS2812B LED panel
   ├── buttons A / B / C / D
   ├── speaker power control
   └── Wi-Fi for sync / diagnostics
        │
        ▼
 Bluetooth speaker
```

The device is offline-first during normal use. Audio and mappings live on the SD card, while Wi-Fi is only used for maintenance, diagnostics, and sync.

## Audio design notes

For the current build I used a `JBL Go 2` connected over Bluetooth. It was a pragmatic choice: I already had that speaker at home, it was collecting dust, and I wanted the sound quality to stay reasonably good instead of dropping in a very cheap `2 USD` speaker.

I originally tried a direct wired path using a `PCM5102A` audio module and a jack connection into that speaker, but I could not get rid of the noise and crackling well enough to be happy with the result. Bluetooth ended up being the cleaner and more reliable solution for this revision.

The long-term plan is to reuse the speaker drivers from the JBL and connect them directly to the ESP32 through an `I2S Audio Amplifier Module NS4168`. The reason for that direction is simple: it should remove the extra Bluetooth speaker from the mechanical stack, give tighter integration with the enclosure, and keep a proper digital audio path from the ESP32 to the amplifier.

The enclosure is designed so the current speaker is slid into dedicated internal holders. Those holders are easy to change, so they can later be replaced with inserts for a simpler ESP32-connected speaker setup or for another self-contained speaker body in a `JBL`-style form factor.

## PCB and schematic

If you want to inspect or modify the hardware design, start here:

- Schematic: [zbox.kicad_sch](hardware/pcb/kicad/zbox.kicad_sch)
- PCB layout: [zbox.kicad_pcb](hardware/pcb/kicad/zbox.kicad_pcb)
- KiCad project: [zbox.kicad_pro](hardware/pcb/kicad/zbox.kicad_pro)
- Manufacturing outputs: [hardware/pcb/gerbers](hardware/pcb/gerbers)
- 3D enclosure export: [zbox_clear.step](hardware/cad/fusion360/step/zbox_clear.step)

## Hardware disclaimer

I am self-taught when it comes to electronics, so the hardware part of this project may still contain mistakes, weak assumptions, or design issues that I am not aware of.

The current revision works for me in real use, but if you want to reuse, manufacture, or adapt it, treat the design as something to review carefully rather than as a guaranteed reference design. In classic programmer terms: it works on my desk.

## Getting started

### Server

```bash
docker compose up -d --build
```

The server runs in Docker. Runtime data is stored in `./data/`, audio assets in `./music/`, and the admin portal files in `./web/`, all mounted into the container by `docker-compose.yml`.

### ESP32 firmware

```bash
cd esp32
pio run
pio run -t upload
pio device monitor
```

The firmware depends on `esp32/lib/ESP32-A2DP`, which is kept as a Git submodule reference to the upstream project.

## Configuration model

- Server runtime configuration is local and installation-specific. See `.env.example` for the current shape.
- Device discovery for the admin portal is configured through the portal itself and persisted on the server.
- The firmware `SERVER_HOST` value in [`esp32/src/zbox_config.h`](esp32/src/zbox_config.h) is a placeholder and must be set for your own deployment before flashing production hardware.

## Modes

- **Card Mode**. Default playback mode. The device reacts to NFC tags. Placing a known card starts the assigned track from the SD card.
- **Music Mode**. Library playback mode. NFC is ignored and the buttons control pause, previous, and next track inside the local music library.
- **Light Mode**. Night light mode. Started from deep sleep by holding `D` longer during wake. In this mode `C` and `D` change brightness instead of volume.
- **Sync Mode**. Service mode used by the admin portal for maintenance, diagnostics, and sync-related operations.

## Button shortcuts

Order: `A B C D`

- <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:3.4em; line-height:1; vertical-align:middle;">•</span> - hold `0.8-2 sec` while waking from deep sleep - **Power on** - works from deep sleep only.
- <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:3.4em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> - `2 sec`, then release - **Power off** - works in normal playback sessions.
- <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:3.4em; line-height:1; vertical-align:middle;">•</span> - hold `>= 2 sec` while waking from deep sleep - **Turn night light on** - works from deep sleep only.
- <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:3.4em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> - `1 sec` - **Turn night light off** - works in Night Light mode only.
- <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:3.4em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> - `2 sec` - **Change mode: Music Mode / Card Mode** - works in normal playback sessions.
- <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:3.4em; line-height:1; vertical-align:middle;">•</span> - short press - **Light +** - works in Night Light mode only.
- <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:3.4em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> - short press - **Light -** - works in Night Light mode only.
- <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:3.4em; line-height:1; vertical-align:middle;">•</span> - short press - **Vol +** - works in normal playback sessions.
- <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:3.4em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> - short press - **Vol -** - works in normal playback sessions.
- <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:3.4em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> - double press - **Next song** - works in Music Mode only.
- <span style="font-size:3.4em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> - double press - **Previous song** - works in Music Mode only.
- <span style="font-size:3.4em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> - short press - **Pause / Resume** - works in Music Mode only.
- <span style="font-size:3.4em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:3.4em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> <span style="font-size:1.35em; line-height:1; vertical-align:middle;">•</span> - `2 sec` - **Sync mode** - service and maintenance mode.

## LED animations

- **Boot progress**. Blue step-by-step progress. Each completed startup stage stays lit, and the current stage blinks three times.
- **Waiting for Bluetooth**. Soft blue breathing animation while the device is waiting for the speaker connection.
- **Idle**. Calm green breathing animation when the device is ready but not currently playing.
- **Playing**. Animated rainbow ring that reacts to audio energy and beats during playback.
- **Night Light**. Solid warm orange light. Brightness is controlled by `C` and `D`.
- **Volume**. Temporary white bar showing the current volume level for about one second after `Vol +` or `Vol -`.
- **Mode change**. Two short flashes. Music Mode uses the Music color, Card Mode uses the Card color.
- **Sleep ready**. Red blinking animation after holding `C` for two seconds. Releasing `C` at that point enters normal deep sleep.
- **Wi-Fi sync**. Yellow blinking while Wi-Fi is active for sync or maintenance.
- **Sync progress**. Blue progress bar that fills as files are transferred.
- **Warning**. Two orange flashes when a mapping or file is missing.
- **Sync mode**. Purple animated service pattern used during sync and maintenance mode.
- **Shutdown**. Purple sweep animation before the device powers down.
- **Battery check**. A color-coded bar on the LEDs: blue for high charge, then green, yellow, orange, and red for critical battery.

## Emergency deep sleep

**Emergency deep sleep** is a forced low-power shutdown triggered by holding `C` for `10 sec`.

- It is intended as a fallback if the normal shutdown path is not enough.
- It skips the regular graceful flow and shuts the device down as directly as possible.
- It still tries to turn the speaker off first if the hardware status line says the speaker is on.

## Deployment shape

The intended deployment is:

1. Build and run the FastAPI server in Docker on a small Linux host such as a Raspberry Pi.
2. Flash the ESP32 with firmware configured to reach that server.
3. Use the web admin portal to manage tracks, NFC mappings, system sounds, and sync operations.

## Documentation

- [Server docs](docs/server.md)
- [ESP32 firmware docs](docs/esp32-firmware.md)
- [Hardware docs](docs/hardware.md)
- [ESP32 quick start](esp32/README.md)

## Known constraints

- Playback is offline-first. The device syncs files and metadata to the SD card instead of streaming during normal use.
- Bluetooth and Wi-Fi are intentionally not used at the same time on the ESP32 due to memory and radio constraints.
- The current firmware configuration still requires a compile-time server host value.
