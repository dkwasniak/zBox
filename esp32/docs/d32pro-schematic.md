# LOLIN D32 Pro — Schematic Reference

**Board**: LOLIN D32 Pro, REV 2.0.0, Date: 2018-06-13
**MCU module**: ESP32-WROVER (ESP32 dual-core 240 MHz, 16 MB Flash @ 1.8 V, 8 MB PSRAM @ 1.8 V)
**Supply**: 3.3 V I/O, 5 V USB input, 3.7 V LiPo battery
**Dimensions**: 65 × 25.4 mm, Weight: 7.5 g

---

## Block 1: USB – UART

**Connector P2**: USB Micro B (USB-MICRO_5P)
- VBUS (pin 1) → power path
- D+ (pin 2), D- (pin 3) → CH340C
- ID (pin 4): not connected
- GND (pin 5)

**IC U3**: CH340C USB-to-UART bridge
- No external crystal required (internal RC oscillator)
- VCC (pin 16): connected to VBUS (5 V)
- GND (pin 1)
- UD+ / UD- (pins 5/6): USB differential pair
- TXD (pin 2): → ESP32 IO3 / RXD0
- RXD (pin 3): ← ESP32 IO1 / TXD0
- V3 (pin 4): internal 3.3 V reference output — bypass cap 100 nF to GND
- DTR# (pin 13): active-low → Auto Flash circuit (controls EN via transistor)
- RTS# (pin 14): active-low → Auto Flash circuit (controls IO0 via transistor)
- CTS#, DSR#, RI#, DCD# (pins 9–12): not connected on this board
- R232 (pin 15): RS-232 level output, not used

**Decoupling**:
- C9: 10 µF electrolytic — bulk cap on VCC line
- C10: 100 nF ceramic — high-frequency bypass on VCC

---

## Block 2: Power

**Power path overview**: VBUS (USB 5 V) and VBAT (LiPo 3.7–4.2 V) are OR-ed through CJ2301 + B5819 to feed the ME6211 LDO.

**Q (CJ2301)**: P-channel MOSFET — power path switch
- Source: VBAT
- Drain: feeds into power rail
- Gate: pulled toward VBUS via two 100 kΩ resistors (voltage divider)
  - When VBUS present and VBUS > VBAT: gate voltage ≈ VBUS → Vgs ≈ 0 → MOSFET OFF → battery disconnected from power rail
  - When no VBUS: gate pulled toward GND via divider → MOSFET ON → battery feeds power rail

**D2**: B5819 Schottky diode
- Anode: VBUS (USB 5 V after connector)
- Cathode: power rail input
- Forward drop: ~0.3 V
- Purpose: allows VBUS to power the rail; blocks reverse current from battery into USB

**Two 100 kΩ resistors**: form gate bias divider for CJ2301

**U2**: ME6211 — 300 mA LDO 3.3 V regulator
- IN: power rail (~4–5 V from VBUS or VBAT path)
- OUT: +3V3 system rail
- EN: enable (HIGH = on); tied or controlled
- BP: bypass pin for internal reference (100 nF cap recommended)
- C3: 1 µF ceramic — input decoupling
- C1: 1 µF ceramic — output decoupling / stability

**Output**: +3V3 rail powering MCU, SD card, TFT port, I2C port, and pull-ups

---

## Block 3: Battery

**U5**: TP4054 — single-cell Li-Ion/LiPo charger (linear, up to 500 mA)
- VCC: connected to VBUS (5 V USB) — charger only active when USB connected
- CHRG (open-drain output): active LOW when charging → drives LED2 via current-limiting resistor
- STDBY (open-drain output): active LOW when charge complete (not explicitly wired to LED on this revision)
- PROG pin: sets charge current: **I_CHG = 1000 V / R_PROG** → R_PROG ≈ 2 kΩ → I_CHG = 500 mA
- BAT pin: connects to VBAT rail and C2 (10 µF ceramic bulk cap) and P1 positive terminal

**P1**: JST PH-2 (2.0 mm pitch) — LiPo battery connector
- Pin 1: BAT+ → VBAT rail
- Pin 2: GND

**C2**: 10 µF — bulk decoupling on VBAT rail

**Battery ADC divider**:
- R12: 100 kΩ from VBAT to IO35 / I35
- R17: 100 kΩ from IO35 / I35 to GND
- IO35 therefore reads approximately VBAT / 2 for firmware battery measurement

**LED2**: charge status LED
- Anode: +3V3 (or VBUS)
- Cathode: CHRG pin via current-limiting resistor (~1 kΩ)
- LED ON = charging; LED OFF = full or no USB

**R_PROG**: ~2 kΩ from PROG pin to GND → sets max charge current = 500 mA

**R6**: resistor near VBUS input (input filtering / ESD)

**C6**: capacitor near VBUS (input bypass for charger)

---

## Block 4: Auto Flash

**U4**: UMH3N — dual NPN digital transistor array (pre-biased: built-in base and emitter resistors ~10 kΩ / ~10 kΩ)

**Purpose**: enables automatic entry to ESP32 bootloader during firmware upload (used by esptool / PlatformIO)

**Circuit — Transistor 1 (RTS# → IO0)**:
- Base: RTS# from CH340C (active LOW)
- Collector: IO0 (ESP32 strapping pin, boot mode)
- Emitter: GND
- Logic: RTS# LOW → T1 ON → IO0 pulled LOW → download mode enabled

**Circuit — Transistor 2 (DTR# → EN)**:
- Base: DTR# from CH340C (active LOW)
- Collector: EN (reset pin, active LOW)
- Emitter: GND
- Logic: DTR# LOW → T2 ON → EN pulled LOW → reset

**Auto-flash sequence** (esptool protocol):
1. DTR=1, RTS=0 → T2 OFF, T1 ON → IO0=LOW (boot mode set)
2. DTR=0, RTS=1 → T2 ON, T1 OFF → EN=LOW (reset pulse)
3. DTR=1, RTS=1 → both OFF → EN=HIGH → ESP32 starts in download mode
4. Upload completes; esptool sends normal reset to boot from flash

---

## Block 5: Core (ESP32-WROVER)

**U9**: ESP32-WROVER module, 38-pin SMD
- Dual-core Xtensa LX6, up to 240 MHz
- Integrated Wi-Fi 802.11 b/g/n and Bluetooth 4.2 (BR/EDR + BLE)
- **16 MB SPI Flash @ 1.8 V** (external to ESP32 die, inside WROVER can)
- **8 MB PSRAM @ 1.8 V** (external, inside WROVER can)
- **IMPORTANT**: FLASH and PSRAM operate at 1.8 V — do NOT apply 3.3 V directly to their SPI bus lines. The ESP32 die handles level translation internally.
- GPIO voltage: 3.3 V (all I/O pins)

**Reset circuit**:
- K1: tactile RESET button → connects EN to GND when pressed
- C4: 100 nF ceramic — debounce capacitor on EN line
- R1: 10 kΩ pull-up — EN to +3V3 (ensures stable HIGH when button released)

**Power decoupling at module**:
- C7: 10 µF — bulk cap on 3V3 near module
- C8: 100 nF ceramic — high-frequency bypass on 3V3 near module

**Built-in LED**: GPIO5 drives onboard blue LED (active HIGH)

**Key UART pins routed to CH340C**:
- IO1 (TXD0): ESP32 → CH340C RXD
- IO3 (RXD0): ESP32 ← CH340C TXD

---

## Block 6: IO — GPIO Headers

Two 1×16P headers: JP1 (left side) and JP2 (right side). All signals at 3.3 V logic.

### JP1 — Left header (top to bottom)

| Pin | Signal | Notes |
|-----|--------|-------|
| 1 | RST | EN — active LOW reset |
| 2 | IO32 / TFT_LED | TFT backlight PWM; RTC-capable; output only recommended |
| 3 | IO22 / SCL | I2C clock; also routed to I2C port P3 |
| 4 | IO21 / SDA | I2C data; also routed to I2C port P3 |
| 5 | IO17 | UART2 TX; general purpose output |
| 6 | IO14 / TFT_CS | TFT chip select; also routed to TFT port P4; strapping pin (MTMS) — must be HIGH or float at boot |
| 7 | VBAT | Raw battery voltage (3.7–4.2 V on battery, ~4.7 V on USB after diode) |
| 8–9 | GND | Ground |
| 10 | IO15 | Strapping pin (MTDO) — internal pull-up; must be HIGH at boot (add 10 kΩ pull-down on custom PCB if peripheral pulls it LOW) |
| 11 | IO13 | General purpose; JTAG MTCK |
| 12 | IO12 | Strapping pin (MTDI) — **must be LOW at boot** (selects flash voltage); internal pull-down; do NOT pull HIGH at boot |
| 13 | IO2 | Strapping pin — must be LOW or float at boot (internal pull-down) |
| 14 | IO0 | Strapping pin — HIGH=normal boot, LOW=download mode; controlled by Auto Flash circuit |
| 15 | +3V3 | 3.3 V regulated output |
| 16 | GND | Ground |

### JP2 — Right header (top to bottom)

| Pin | Signal | Notes |
|-----|--------|-------|
| 1 | IO23 / MOSI | SPI MOSI — shared bus (TFT and SD card) |
| 2 | IO22 / SCL | Same GPIO22 as JP1 pin 3 (both headers expose it) |
| 3 | IO5 / LED | Built-in blue LED (active HIGH); strapping pin — must be LOW at boot |
| 4 | IO19 / MISO | SPI MISO — shared bus (TFT and SD card) |
| 5 | IO18 / SCK | SPI clock — shared bus (TFT and SD card) |
| 6 | IO25 | DAC1; analog output; RTC-capable |
| 7 | IO4 / TF_CS | SD card SPI chip select |
| 8 | IO16 | UART2 RX; general purpose |
| 9 | +3V3 | 3.3 V regulated output |
| 10–11 | GND | Ground |
| 12 | IO33 / TFT_RST | TFT reset; also routed to TFT port P4; RTC-capable; ADC1_CH5 (input only on ADC) |
| 13 | IO27 / TFT_DC | TFT Data/Command select; also routed to TFT port P4; RTC-capable |
| 14 | IO26 | DAC2; RTC-capable |
| 15 | IO35 | **Input only** — no internal pull-up/pull-down; ADC1_CH7; connected to onboard VBAT/2 divider |
| 16 | IO34 | **Input only** — no internal pull-up/pull-down; ADC1_CH6 |

---

## Block 7: Micro SD Card

**Connector**: MICROSD_CNF1_NOCD (standard micro SD push-push, no card-detect switch variant)

**Interface**: SPI (hardware SPI, shared with TFT)

**Signal mapping**:

| SD Pin | Signal | ESP32 GPIO |
|--------|--------|------------|
| CS | TF_CS | IO4 |
| DI (CMD) | MOSI | IO23 |
| CLK | SCK | IO18 |
| DO (DAT0) | MISO | IO19 |
| VCC | +3V3 | — |
| GND | GND | — |

**Pull-up resistors**: R2, R3, R8, R10 — each **10 kΩ** to +3V3
- R2: CS line pull-up
- R3: MOSI line pull-up
- R8: SCK line pull-up
- R10: MISO line pull-up
- Purpose: ensure defined logic levels when no card inserted; required by SD SPI spec

**Card detect**: S1 — micro switch (short to GND when card inserted) → CD1 / CD2 terminals
- Not connected to any ESP32 GPIO on this revision (NOCD variant in firmware use)
- CD1, CD2: decoupling caps to GND on card detect lines

**Firmware usage**:
```cpp
SPI.begin(18, 19, 23, 4);  // SCK, MISO, MOSI, SS
SD.begin(4);               // CS = GPIO4
```
GPIO 18/19/23 are internally routed to the SD slot. Do NOT use these GPIOs for other peripherals while SD is active unless using separate CS lines.

---

## Block 8: TFT LCD Port

**Connector P4**: SH1.0-10-LI (10-pin, 1.0 mm pitch JST SH)

| P4 Pin | Signal | ESP32 GPIO | Notes |
|--------|--------|------------|-------|
| 1 | GND | — | |
| 2 | +3V3 | — | |
| 3 | MOSI | IO23 | SPI MOSI (shared with SD) |
| 4 | SCK | IO18 | SPI clock (shared with SD) |
| 5 | MISO | IO19 | SPI MISO (shared with SD) |
| 6 | TFT_CS | IO14 | TFT chip select |
| 7 | TFT_DC | IO27 | Data/Command select (D/C or RS pin) |
| 8 | TFT_RST | IO33 | TFT hardware reset (active LOW) |
| 9 | TFT_LED | IO32 | Backlight control (PWM) |
| 10 | +3V3 | — | Second 3V3 pin for display VCC |

SPI bus shared with SD card — differentiated by CS: IO14 (TFT) vs IO4 (SD). Only one CS active at a time.

Compatible displays: LOLIN TFT 1.4" (ST7735, 128×128) or similar 1.0 mm pitch SH connector TFTs.

---

## Block 9: I2C Port

**Connector P3**: SH1.0-4 (4-pin, 1.0 mm pitch JST SH)

| P3 Pin | Signal | ESP32 GPIO |
|--------|--------|------------|
| 1 | GND | — |
| 2 | SDA | IO21 |
| 3 | SCL | IO22 |
| 4 | +3V3 | — |

No pull-up resistors on the D32 Pro board itself for I2C — must be provided externally (typically 4.7 kΩ to 3.3 V) or enabled via ESP32 internal pull-ups.

Compatible with LOLIN I2C sensors (SHT30, BMP280, etc.) using the same SH1.0-4 connector.

---

## Critical Notes for Firmware / Agent Reference

### Strapping pins — boot constraints

| GPIO | Constraint | Risk |
|------|-----------|------|
| IO0 | Must be HIGH (or floating) for normal boot | Connected to Auto Flash circuit — do not pull LOW permanently |
| IO2 | Must be LOW or float at boot | Has internal pull-down |
| IO5 | Internal pull-up → HIGH at boot (normal); affects SDIO slave timing only | Built-in LED (active HIGH); not critical for SPI-flash boot |
| IO12 | **Must be LOW at boot** | Selects 3.3 V flash voltage. If pulled HIGH: ESP32 expects 1.8 V flash → crash |
| IO14 | Must be HIGH or float at boot | Used as TFT_CS — TFT library must not assert CS during reset |
| IO15 | Should be HIGH at boot | Has internal pull-up; custom PCB may add 10 kΩ pull-down on IO15 |

### Input-only GPIOs (no internal pull-up)
IO34 and IO35 are input-only with no pull-up/pull-down. Use external resistors when needed.

### Shared SPI bus
IO18 (SCK), IO19 (MISO), IO23 (MOSI) are wired to **both** the SD card slot and the TFT port. Never activate both CS pins simultaneously.

### Flash and PSRAM voltage
Both Flash and PSRAM inside the ESP32-WROVER operate at **1.8 V**. The schematic explicitly notes: *"The FLASH & PSRAM work at 1.8V"*. This is handled internally by the WROVER module. Required build flags: `-DBOARD_HAS_PSRAM=1 -mfix-esp32-psram-cache-issue`.

### Battery voltage on VBAT pin
VBAT on JP1 is raw LiPo voltage (3.7–4.2 V nominal). When powered via USB without a battery connected, VBAT may read higher (~4.7 V). Do not feed VBAT directly to 3.3 V-only peripherals.

### Charging indicator
LED2 (CHRG from TP4054) is active-LOW — LED is ON while charging, OFF when full or USB disconnected.

### Charge current
TP4054 PROG resistor ≈ 2 kΩ → max charge current = **500 mA**. Do not replace R_PROG with a lower value without thermal verification.

### I2C pull-ups
The D32 Pro board has no onboard I2C pull-up resistors. Add 4.7 kΩ to 3.3 V externally, or enable `Wire.begin()` with `INPUT_PULLUP` (internal ~45 kΩ, adequate only for short buses at low speed).

---

## Relationship to other project docs

| Topic | File |
|-------|------|
| Custom PCB pinout, project GPIO assignments | `docs/hardware.md` |
| Firmware architecture, audio stack, build flags | `docs/esp32-firmware.md` |
| This file — D32 Pro base board internal schematic | `esp32/docs/d32pro-schematic.md` |
