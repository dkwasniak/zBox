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
| R12 | 10k | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal` | szereg bramki Q3 (soft-start z C6) |
| R13 | 100k | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal` | pull-down bramki Q4 |
| R14 | 100R | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal` | szereg bramki Q4 |
| C6 | 100nF | `Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm` | soft-start bramki Q3 (G↔S) |
| C2 | 10uF (10–47uF) | `Capacitor_THT:CP_Radial_D5.0mm_P2.50mm` | bulk NS (przy module) |
| C3 | 100nF | `Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm` | HF NS (przy module) |
| C4 | 10uF | `Capacitor_THT:CP_Radial_D5.0mm_P2.50mm` | bulk NFC |
| C5 | 100nF | `Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm` | HF NFC |

BOM: 1× AO3401A · 1× AO3400A · 2× 100k · 1× 10k · 1× 100R · 3× 100nF · 2× 10uF.

> **Soft-start Q3 (obowiązkowy).** R12 = **10k** (nie 100R) + **C6 100nF** bramka↔źródło dają
> rampę załączenia ~1 ms. Bez tego NS4168 przy `nsPowerOn()` łapie inrush na współdzielonym railu
> `+3V3` → detektor brownout ESP resetuje płytkę w pętli. Zasada: **większy C2 → wolniejszy soft-start**
> (C2 220µF wymaga C6 ≈ 470nF). Przy C6 100nF trzymaj C2 ≤ ~47µF. Szczegóły niżej.

## Połączenia — NS4168 (Q3, high-side, GPIO15, aktywne LOW)

- Q3: S → `+3V3`, D → `+3V3_NS`, G → `NS_EN_G`
- R11: `NS_EN_G` ↔ `+3V3` (pull-up → OFF przy Hi-Z)
- R12 (**10k**): `GPIO15` (J9.15) ↔ `NS_EN_G`
- C6 (**100nF**): `NS_EN_G` ↔ `+3V3` (bramka↔źródło, soft-start)
- C2 (**10µF**): `+3V3_NS` ↔ `GND` — **wprost na pinach V/GND modułu**
- C3 (**100nF**): `+3V3_NS` ↔ `GND` — **wprost na pinach V/GND modułu**
- Zmiana netu: **J13.p4 (NS4168 VDD) `+3V3` → `+3V3_NS`**

Prąd: `+3V3` → S → D → `+3V3_NS` → VDD modułu NS. Brama G decyduje o załączeniu.
R12 + C6 spowalniają załączenie (rampa ~1 ms) → prąd rozruchowy ≈ C2·(3,3V/czas) mały → rail nie siada.
C2/C3 lutuj jak najbliżej modułu (lokalny rezerwuar).

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
- R12/R14 chronią bramki; R12 (10k) + C6 dodatkowo robią soft-start Q3.
- **Czemu NS musi mieć soft-start, a NFC/LED nie:** NS4168 wisi na `+3V3` — **tym samym railu co rdzeń ESP**,
  którego pilnuje detektor brownout. Skokowy inrush przy `nsPowerOn()` sadza ten rail → reset. NFC ciągnie
  znikomy prąd, a LED-boost (patrz `stepup_replace.md`) ma soft-start w chipie i siedzi na `VBAT` (osobny rail,
  ESP go nie pilnuje). Sztywne wpięcie NS do `+3V3` działało bo amp wstawał **razem z narastającym railem**
  przy cold-boocie (brak zdarzenia przełączenia) — ale żarło prąd w śnie. Load-switch + soft-start = oszczędność
  w śnie **oraz** łagodne załączanie.
- Linie danych (I2S → NS, SPI → NFC) bez zmian; przełączane jest tylko zasilanie/masa modułu.
- Pull-upy przycisków R9/R10 zostają na nieprzełączanym `+3V3`.

## Firmware (`esp32/src/zbox_config.h`, `esp32/src/power/peripheral_power.cpp`)

- `#define NS_EN 15` (aktywne LOW), `#define NFC_EN 12` (aktywne HIGH).
- **NS OFF = Hi-Z (`pinMode(NS_EN, INPUT)`), nie OUTPUT-HIGH.** `pinMode(OUTPUT)` na chwilę zatrzaskuje
  pin na LOW zanim `digitalWrite(HIGH)` zdąży zadziałać — przez R12 to **na µs załącza Q3** → inrush →
  brownout w pętli przy boocie. Hi-Z zdaje wyłączanie na R11 (pull-up trzyma bramkę w +3V3 = FET off),
  bez żadnego glitcha. Dotyczy `peripheralPowerInitEarly()` i `nsPowerOff()`.
- **NS ON:** najpierw `digitalWrite(NS_EN, LOW)`, potem `pinMode(NS_EN, OUTPUT)` — pin nigdy nie mrugnie HIGH.
- Wybudzenie NS: ON → zwłoka ~20–50 ms → `i2s.begin()`. Soft-start (R12 10k + C6) daje miękkie załączenie.
- Wybudzenie NFC: OUTPUT → `NFC_EN = HIGH` → zwłoka ~20–50 ms → init SPI/NFC.
- Przed snem:
  - NS (high-side): deinit I2S → linie **LOW** → `NS_EN` w **Hi-Z (INPUT)** (off, R11 trzyma FET wyłączony).
  - NFC (low-side): deinit SPI → linie w **hi-Z (INPUT)** → `NFC_EN = LOW` (off). Nie LOW na liniach SPI! Masa modułu unosi się ku +3V3; linie na LOW forward-biasowałyby diody ESD (GND→pin) i back-powerowały ESP. Hi-Z pływa razem z masą modułu.
