# Checklista do naniesienia w schemacie KiCad — load-switche NS + NFC

Mechaniczna lista do wklepania w `hardware/pcb/kicad/zbox.kicad_sch`. Bez step-upa.
Źródło prawdy: [`load-switches.md`](load-switches.md). Kolejność: nety → elementy → połączenia → zmiany na złączach.

## 0. Nowe nety do utworzenia

| Net | Rola |
|---|---|
| `+3V3_NS` | zasilanie modułu NS4168 za tranzystorem Q3 (przełączane) |
| `NS_EN_G` | bramka Q3 |
| `NFC_GND_SW` | masa modułu PN532 za tranzystorem Q4 (przełączana) |
| `NFC_EN_G` | bramka Q4 |

Istniejące, używane bez zmian: `+3V3`, `GND`, `GPIO15` (J9.15), `GPIO12` (J8.12).

## 1. Elementy do dodania

| Ref | Symbol / wartość | Footprint |
|---|---|---|
| Q3 | AO3401A (P-MOSFET) | `Package_TO_SOT_SMD:SOT-23` |
| Q4 | AO3400A (N-MOSFET) | `Package_TO_SOT_SMD:SOT-23` |
| R11 | 100k | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal` |
| R12 | 10k | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal` |
| R13 | 100k | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal` |
| R14 | 100R | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal` |
| C6 | 100nF | `Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm` |
| C2 | 10µF (elektrolit) | `Capacitor_THT:CP_Radial_D5.0mm_P2.50mm` |
| C3 | 100nF | `Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm` |
| C4 | 10µF (elektrolit) | `Capacitor_THT:CP_Radial_D5.0mm_P2.50mm` |
| C5 | 100nF | `Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm` |

BOM: 1× AO3401A · 1× AO3400A · 2× 100k · 1× 10k · 1× 100R · 3× 100nF · 2× 10µF.

## 2. Pinout FET-ów (SOT-23)

Oba: **pin1 = G, pin2 = S, pin3 = D.**

## 3. Połączenia pin-po-pinie

### NS — Q3 (high-side P-FET, aktywne LOW, soft-start)

| Od | Net |
|---|---|
| Q3 pin2 (S) | `+3V3` |
| Q3 pin3 (D) | `+3V3_NS` |
| Q3 pin1 (G) | `NS_EN_G` |
| R11 | `NS_EN_G` ↔ `+3V3` |
| R12 (10k) | `GPIO15` (J9.15) ↔ `NS_EN_G` |
| C6 (100nF) | `NS_EN_G` ↔ `+3V3` |
| C2 (10µF) | `+3V3_NS` (**+**) ↔ `GND` (**–**) |
| C3 (100nF) | `+3V3_NS` ↔ `GND` |

### NFC — Q4 (low-side N-FET, aktywne HIGH)

| Od | Net |
|---|---|
| Q4 pin3 (D) | `NFC_GND_SW` |
| Q4 pin2 (S) | `GND` |
| Q4 pin1 (G) | `NFC_EN_G` |
| R13 | `NFC_EN_G` ↔ `GND` |
| R14 (100R) | `GPIO12` (J8.12) ↔ `NFC_EN_G` |
| C4 (10µF) | `+3V3` (**+**) ↔ `NFC_GND_SW` (**–**) |
| C5 (100nF) | `+3V3` ↔ `NFC_GND_SW` |

## 4. Zmiany netów na istniejących połączeniach (KRYTYCZNE)

Nie zapomnij przepiąć zasilania modułów — inaczej switche nic nie robią:

- **NS4168 VDD:** `J13.p4` z `+3V3` → **`+3V3_NS`**.
- **PN532 GND:** `J3.p2` z `GND` → **`NFC_GND_SW`** (p1 zostaje `+3V3`).

## 5. Sanity-check po naniesieniu (ERC + logika)

- `+3V3_NS` łączy: Q3.D, C2+, C3, VDD NS4168 — i **nic** poza tym (żadnego `+3V3`).
- `NFC_GND_SW` łączy: Q4.D, C4–, C5, GND PN532 — i **nic** poza tym (żadnego `GND`).
- `NS_EN_G` łączy tylko: Q3.G, R11, R12, C6.
- `NFC_EN_G` łączy tylko: Q4.G, R13, R14.
- Bramki wyjściowe: `GPIO15`→R12, `GPIO12`→R14 (nie wprost do bramki).
- Elektrolity: C2 **+** na `+3V3_NS`, C4 **+** na `+3V3`.
- ERC bez „unconnected"/„conflict" na nowych netach.
