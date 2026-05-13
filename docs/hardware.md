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

## Lolin D32 Pro header numbering from the board photo

The board photo uses `L1..L16` on the left header and `R1..R16` on the right header, counted from top to bottom with the USB connector at the bottom.

### Header labels from the board photo

| Position | Board label | GPIO / signal |
|---|---|---|
| `L1` | `3V3` | `3V3` |
| `L2` | `RST` | `RST` |
| `L3` | `VP` | `GPIO36` |
| `L4` | `VN` | `GPIO39` |
| `L5` | `34` | `GPIO34` |
| `L6` | `32` | `GPIO32` |
| `L7` | `33` | `GPIO33` |
| `L8` | `25` | `GPIO25` |
| `L9` | `26` | `GPIO26` |
| `L10` | `27` | `GPIO27` |
| `L11` | `14` | `GPIO14` |
| `L12` | `12` | `GPIO12` |
| `L13` | `13` | `GPIO13` |
| `L14` | `EN` | `EN` |
| `L15` | `USB` | `5V / USB` |
| `L16` | `BAT` | `VBAT` |
| `R1` | `GND` | `GND` |
| `R2` | `23` | `GPIO23` |
| `R3` | `22` | `GPIO22` |
| `R4` | `TX` | `GPIO1` |
| `R5` | `RX` | `GPIO3` |
| `R6` | `21` | `GPIO21` |
| `R7` | `19` | `GPIO19` |
| `R8` | `18` | `GPIO18` |
| `R9` | `5` | `GPIO5` |
| `R10` | `NC` | `NC` |
| `R11` | `NC` | `NC` |
| `R12` | `4` | `GPIO4` |
| `R13` | `0` | `GPIO0` |
| `R14` | `2` | `GPIO2` |
| `R15` | `15` | `GPIO15` |
| `R16` | `GND` | `GND` |

### Project wiring in `Lx/Rx` format

| Position | Function |
|---|---|
| `L5` | `GPIO34 / JBL_STATUS` |
| `L6` | `GPIO32 / BTN_A` |
| `L7` | `GPIO33 / BTN_B` |
| `L8` | `GPIO25 / BTN_C` |
| `L9` | `GPIO26 / BTN_D` |
| `L10` | `GPIO27 / LED_EN` |
| `L11` | `GPIO14 / LED_PIN` |
| `L13` | `GPIO13 / JBL_POWER` |
| `R3` | `GPIO22 / PN532_SCK` |
| `R6` | `GPIO21 / PN532_MISO` |
| `R9` | `GPIO5 / PN532_SS` |
| `R12` | `GPIO4 / SD_CS` |
| `R13` | `GPIO0 / PN532_MOSI` |

## Speaker wiring

The JBL Go 2 is connected to the PCB on three points:

**Power button** — the two legs of the speaker's physical power button are wired to the PCB. The ESP32 shorts them through an NPN transistor (`BC547`, GPIO `13`) to simulate a button press. This is how the firmware turns the speaker on and off without touching it mechanically.

**Power line (JBL status)** — one wire taps into a point inside the speaker where voltage appears only after the speaker has powered on. This is read by the ESP32 on GPIO `34` (ADC) to detect whether the speaker is actually on. The exact tap point needs to be found with a multimeter — look for a point that reads ~0 V when the speaker is off and a measurable voltage (above the `JBL_STATUS_THRESHOLD` in firmware) when it is on.

**Charging (+/−)** — the speaker's USB charging input is wired to the PCB's USB-C receptacle so the JBL battery can be charged through the zBox's USB-C port, without a separate cable plugged into the speaker.

## Notes

- The PN532 is wired over software SPI: `SS=GPIO5`, `SCK=GPIO22`, `MISO=GPIO21`, `MOSI=GPIO0`.
- The SD card uses the board SPI wiring, so the related SPI pins should not be repurposed.
- The current firmware uses Bluetooth audio only. The older PCM5102A path is not part of the active design.
- LED power is switched so the strip can be fully shut down during sleep.
- The PCB currently uses a `470uF` polarized capacitor (`C1`) in the power path near the step-up / LED supply section.

## Practical constraints

- Keep a shared ground between the ESP32, LED supply, and speaker control circuit.
- Treat wake, button routing, and diagnostic wiring as firmware-coupled decisions. Review [`esp32/src/zbox_config.h`](../esp32/src/zbox_config.h) before changing the PCB.
- If you revise the board, check whether the current button header and optional audio breakout still match the firmware goals.
- KiCad sources live in `hardware/pcb/kicad/`, and production Gerbers live in `hardware/pcb/gerbers/`.
