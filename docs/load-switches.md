# Load switche: sterowanie zasilaniem NS4168 i PN532

Moduł NS (wzmacniacz) i moduł NFC zasilane tylko gdy ESP jest wybudzone. Każdy na osobnym
tranzystorze. Domyślnie OFF w deep sleep i przy starcie/flashu.

Sterowanie:
- **NS_EN = GPIO15** (J9.15) — high-side P-FET, **aktywne LOW** (LOW = on).
- **NFC_EN = GPIO12** (J8.12) — low-side N-FET, **aktywne HIGH** (HIGH = on).

Dobór pinów wymuszony strappingiem: GPIO15 bezpieczny przy HIGH na boot, GPIO12 wymaga LOW
na boot (pull-down = zgodne). GPIO2 odrzucony (pull-up→HIGH blokuje wgrywanie przez USB).

## Elementy i footprinty

| Ref | Wartość | Footprint | Uwaga |
|---|---|---|---|
| Q3 | AO3401A (P-MOSFET) | `Package_TO_SOT_SMD:SOT-23` | switch NS, high-side |
| Q4 | AO3400A (N-MOSFET) | `Package_TO_SOT_SMD:SOT-23` | switch NFC, low-side |
| R11 | 100k | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal` | pull-up bramki Q3 |
| R12 | 100R | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal` | szereg bramki Q3 |
| R13 | 100k | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal` | pull-down bramki Q4 |
| R14 | 100R | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal` | szereg bramki Q4 |
| C2 | 100uF | `Capacitor_THT:CP_Radial_D8.0mm_P3.50mm` | bulk NS |
| C3 | 100nF | `Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm` | HF NS |
| C4 | 1uF | `Capacitor_THT:CP_Radial_D5.0mm_P2.50mm` | bulk NFC |
| C5 | 100nF | `Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm` | HF NFC |

BOM: 1× AO3401A · 1× AO3400A · 2× 100k · 2× 100R · 2× 100nF · 1× 100uF · 1× 1uF.

## Połączenia — NS4168 (Q3, high-side, GPIO15, aktywne LOW)

- Q3: S → `+3V3`, D → `+3V3_NS`, G → `NS_EN_G`
- R11: `NS_EN_G` ↔ `+3V3`
- R12: `GPIO15` (J9.15) ↔ `NS_EN_G`
- C2: `+3V3_NS` ↔ `GND`
- C3: `+3V3_NS` ↔ `GND`
- Zmiana netu: **J13.p4 (NS4168 VDD) `+3V3` → `+3V3_NS`**

Prąd: `+3V3` → S → D → `+3V3_NS` → VDD modułu NS. Brama G decyduje o załączeniu.

## Połączenia — PN532 (Q4, low-side, GPIO12, aktywne HIGH)

- Q4: D → `NFC_GND_SW`, S → `GND`, G → `NFC_EN_G`
- R13: `NFC_EN_G` ↔ `GND`
- R14: `GPIO12` (J8.12) ↔ `NFC_EN_G`
- C4: `+3V3` ↔ `NFC_GND_SW`
- C5: `+3V3` ↔ `NFC_GND_SW`
- Zmiana netu: **J3.p2 (PN532 GND) `GND` → `NFC_GND_SW`** (p1 zostaje `+3V3`)

Prąd wraca: masa modułu PN532 (`NFC_GND_SW`) → D → S → `GND`. Brama G decyduje o załączeniu.
Masa PN532 unosi się o kilka mV (Rds×prąd) — dla software-SPI bez znaczenia.

## Zasady

- Pull-up R11 (Q3) i pull-down R13 (Q4) trzymają OFF, gdy GPIO jest hi-Z (sen, cold boot).
- R12/R14 chronią bramki.
- Linie danych (I2S → NS, SPI → NFC) bez zmian; przełączane jest tylko zasilanie/masa modułu.
- Pull-upy przycisków R9/R10 zostają na nieprzełączanym `+3V3`.

## Firmware (`esp32/src/zbox_config.h`)

- `#define NS_EN 15` (aktywne LOW), `#define NFC_EN 12` (aktywne HIGH).
- Wybudzenie: OUTPUT → włącz → zwłoka ~20–50 ms → init SPI/NFC i `i2s.begin()`.
- Przed snem: deinit I2S/SPI (linie LOW) → wyłącz EN.
