# zBox Hardware

## Main components

- ESP32 Lolin D32 Pro
- PN532 NFC reader over software SPI
- Bluetooth speaker controlled through A2DP and a power transistor
- WS2812B LED strip
- SD card for offline music and mappings
- Custom PCB for wiring and power distribution
- 3D-printed enclosure with exported CAD kept in `hardware/cad/fusion360/step/`
- Current exported model: `hardware/cad/fusion360/step/zbox_clear.step`

## High-level architecture

```text
ESP32 + SD + NFC + LEDs + buttons
        │
        ├── Bluetooth A2DP audio ──> speaker
        └── Wi-Fi only during sync / diagnostics
```

## Important pin assignments

| GPIO | Function |
|---|---|
| `4` | SD card chip select |
| `5` | PN532 chip select |
| `0` | PN532 MOSI |
| `13` | Speaker power transistor |
| `14` | WS2812B data |
| `21` | PN532 MISO |
| `22` | PN532 SCK |
| `25` | Button C |
| `26` | Button D |
| `27` | LED power enable |
| `32` | Button A |
| `33` | Button B |
| `34` | Speaker status ADC |

## Notes

- The PN532 is wired over software SPI: `SS=GPIO5`, `SCK=GPIO22`, `MISO=GPIO21`, `MOSI=GPIO0`.
- The SD card uses the board SPI wiring, so the related SPI pins should not be repurposed.
- The current firmware uses Bluetooth audio only. The older PCM5102A path is not part of the active design.
- LED power is switched so the strip can be fully shut down during sleep.
- The external speaker power button is driven through a transistor rather than a relay.
- The PCB currently uses a `470uF` polarized capacitor (`C1`) in the power path near the step-up / LED supply section.

## Practical constraints

- Keep a shared ground between the ESP32, LED supply, and speaker control circuit.
- Treat wake, button routing, and diagnostic wiring as firmware-coupled decisions. Review [`esp32/src/zbox_config.h`](../esp32/src/zbox_config.h) before changing the PCB.
- If you revise the board, check whether the current button header and optional audio breakout still match the firmware goals.
- KiCad sources live in `hardware/pcb/kicad/`, and production Gerbers live in `hardware/pcb/gerbers/`.
