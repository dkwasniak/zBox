# zBox Hardware

## Main components

- ESP32 Lolin D32 Pro
- PN532 NFC reader over software SPI
- NS4168 I2S amplifier as the default speaker output
- Optional Bluetooth headphones mode over A2DP
- WS2812B LED strip
- SD card for offline music and mappings
- Custom PCB for wiring and power distribution
- 3D-printed enclosure with exported CAD kept in `hardware/cad/fusion360/step/`
- Current exported model: `hardware/cad/fusion360/step/zbox_clear.step`

## High-level architecture

```text
ESP32 + SD + NFC + LEDs + buttons
        │
        ├── I2S audio ──> NS4168 amplifier ──> speaker
        ├── Optional Bluetooth A2DP audio ──> headphones
        └── Wi-Fi only during sync / diagnostics
```

## Important pin assignments

| GPIO | Function |
|---|---|
| `4` | SD card chip select |
| `5` | PN532 chip select |
| `0` | PN532 MOSI |
| `12` | PN532 power enable (NFC_EN, low-side, active HIGH) |
| `13` | NS4168 I2S data out |
| `14` | WS2812B data |
| `15` | NS4168 power enable (NS_EN, high-side, active LOW) |
| `21` | PN532 MISO |
| `22` | PN532 SCK |
| `25` | Button C |
| `26` | Button D |
| `27` | LED power enable |
| `32` | NS4168 I2S bit clock |
| `33` | NS4168 I2S word select |
| `36` | Button A |
| `39` | Button B |

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
| `L3` | `GPIO36 / BTN_A` |
| `L4` | `GPIO39 / BTN_B` |
| `L6` | `GPIO32 / AUDIO_I2S_BCLK` |
| `L7` | `GPIO33 / AUDIO_I2S_LRCK` |
| `L8` | `GPIO25 / BTN_C` |
| `L9` | `GPIO26 / BTN_D` |
| `L10` | `GPIO27 / LED_EN` |
| `L11` | `GPIO14 / LED_PIN` |
| `L13` | `GPIO13 / AUDIO_I2S_DOUT` |
| `R3` | `GPIO22 / PN532_SCK` |
| `R6` | `GPIO21 / PN532_MISO` |
| `R9` | `GPIO5 / PN532_SS` |
| `R12` | `GPIO4 / SD_CS` |
| `R13` | `GPIO0 / PN532_MOSI` |

## Audio wiring

The current board revision assumes a direct digital audio path:

- `GPIO32` -> `NS4168 BCLK`
- `GPIO33` -> `NS4168 LRCK / WS`
- `GPIO13` -> `NS4168 DIN`

The old JBL-specific control wiring is no longer part of the active firmware design:

- `GPIO13` is reused for I2S data.
- `GPIO34` is free from audio duties.
- There is no firmware dependency on a speaker power transistor or speaker status ADC.

## Power switching (load switches)

Goal: the NS4168 amplifier and the PN532 NFC reader are powered only while the ESP32 is
awake, each on its own transistor. They stay off in deep sleep and default off at
power-on/flash.

Pin choice is constrained by ESP32 strapping: the only free output-capable GPIOs are
`2`, `12`, `15` (`16/17` are PSRAM on the D32 Pro, `34` is input-only, the rest are used).
- `NS_EN = GPIO15` — high-side P-FET, gate pull-up to `+3V3` (default OFF); HIGH at boot is
  safe. **Active LOW** (LOW = on).
- `NFC_EN = GPIO12` — MTDI must be LOW at boot, so PN532 uses a **low-side N-FET** with a
  gate pull-down. **Active HIGH** (HIGH = on).
- `GPIO2` was rejected: a pull-up to `+3V3` (HIGH at boot) can block USB flashing.

### NS4168 amplifier — high-side P-FET (Q3), `GPIO15`

| Part | Value | Connection |
|---|---|---|
| Q3 | AO3401A (P-MOSFET, SOT-23) | S -> `+3V3`, D -> `+3V3_NS`, G -> `NS_EN_G` |
| R11 | 100k | `NS_EN_G` <-> `+3V3` (gate pull-up, default OFF) |
| R12 | 100R | `GPIO15` <-> `NS_EN_G` (series gate) |
| C2 | 220uF | `+3V3_NS` <-> `GND` (bulk near J13) |
| C3 | 100nF | `+3V3_NS` <-> `GND` (HF decoupling near J13) |

Net change: **J13 (NS4168) VDD pin `+3V3` -> `+3V3_NS`**.

### PN532 NFC reader — low-side N-FET (Q4), `GPIO12`

| Part | Value | Connection |
|---|---|---|
| Q4 | AO3400A (N-MOSFET, SOT-23) | D -> `NFC_GND_SW`, S -> `GND`, G -> `NFC_EN_G` |
| R13 | 100k | `NFC_EN_G` <-> `GND` (gate pull-down, default OFF + strap LOW) |
| R14 | 100R | `GPIO12` <-> `NFC_EN_G` (series gate) |
| C4 | 10uF | `+3V3` <-> `NFC_GND_SW` (bulk near J3) |
| C5 | 100nF | `+3V3` <-> `NFC_GND_SW` (HF decoupling near J3) |

Net change: **J3 (PN532) GND pin `GND` -> `NFC_GND_SW`** (VDD pin stays `+3V3`).

Firmware (`esp32/src/zbox_config.h`): add `NS_EN 15` (active LOW) and `NFC_EN 12`
(active HIGH). On wake: enable, wait ~20-50 ms, then init SPI/NFC and `i2s.begin()`. Before
sleep:
- NS (high-side): deinit I2S -> lines **LOW** -> `NS_EN = HIGH` (off). LOW is safe because
  the module VDD is 0.
- NFC (low-side): deinit SPI -> lines **hi-Z (INPUT)** -> `NFC_EN = LOW` (off). Not LOW! The
  module ground floats toward `+3V3`, so driving the lines LOW would forward-bias the module
  ESD diodes (GND->pin) and back-power the ESP. Hi-Z lets the lines float with the module.

Pull-up/pull-down hold both off in sleep and at cold boot.

### Status

- [x] Audio path I2S -> NS4168 (`GPIO32/33/13`), speaker via NS4168 module (J13)
- [x] Buttons A/B moved to L3/L4 (`GPIO36/39`) with 10k external pull-ups (R9/R10)
- [x] JBL circuit removed (Q1, R3/R6/R7, J10, J12, all `JBL_*` nets)
- [ ] NS4168 load switch: Q3, R11, R12, C2, C3; J13 VDD -> `+3V3_NS`
- [ ] PN532 load switch: Q4, R13, R14, C4, C5; J3 GND -> `NFC_GND_SW`
- [ ] Correct `C1` value in the schematic to the fitted `470uF`
- [ ] Firmware: add `NS_EN` / `NFC_EN` and the wake/sleep power sequencing

## Notes

- The PN532 is wired over software SPI: `SS=GPIO5`, `SCK=GPIO22`, `MISO=GPIO21`, `MOSI=GPIO0`.
- The SD card uses the board SPI wiring, so the related SPI pins should not be repurposed.
- The current firmware starts on local I2S audio immediately. Bluetooth is only an on-demand headphones mode triggered by a long hold on `BTN_A`.
- LED power is switched so the strip can be fully shut down during sleep.
- The PCB currently uses a `470uF` polarized capacitor (`C1`) in the power path near the step-up / LED supply section.

## Practical constraints

- Keep a shared ground between the ESP32, LED supply, and speaker control circuit.
- Treat wake, button routing, and diagnostic wiring as firmware-coupled decisions. Review [`esp32/src/zbox_config.h`](../esp32/src/zbox_config.h) before changing the PCB.
- If you revise the board, check whether the current button header and optional audio breakout still match the firmware goals.
- KiCad sources live in `hardware/pcb/kicad/`, and production Gerbers live in `hardware/pcb/gerbers/`.
