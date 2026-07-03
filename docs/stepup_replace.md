# Wymiana step-up: MT3608 → TPS613222A

Wymiana dużego modułu MT3608 (z potencjometrem) na mały, stały 5 V boost. Przy okazji:
w śnie odcinamy cały podsystem LED istniejącym tranzystorem przeniesionym na **wejście**
boosta. Zachowujemy `C1 470 µF` (fix na piszczenie głośnika przy animacji diod).

## Elementy — bilans

**USUŃ:**
- moduł MT3608 (U2 stary) — razem z nim znika potencjometr

**DODAJ:**

| Ref | Wartość | Obudowa | Rola |
|---|---|---|---|
| U2 | TPS613222A | SOT-23-6 | boost stałe 5 V (bez potu) |
| L1 | 2,2 µH, Isat ≥1,5–2 A | SMD | cewka boosta (była w module) |
| Cin | 10 µF | ceramik | filtr wejścia |
| Cout | 22 µF | ceramik | stabilność pętli boosta |
| Rg | 100R | — | szereg bramki Q1 |

**PRZENIEŚ (istniejące):**
- Q1 (AO3415A P-FET) i R8 (100k) — z wyjścia `+5V_LED` na **wejście** boosta

**ZOSTAW bez zmian:**
- `C1 470 µF` — rezerwuar anti-pisk, przy pasku na `+5V_LED`
- pasek WS2812B, `LED_PIN` = GPIO14 (dane), load-switche NS/NFC

## Nety

| Net | Opis |
|---|---|
| `VBAT` | + ogniwa (3,0–4,2 V) |
| `BOOST_IN` | wejście boosta za tranzystorem (przełączane) |
| `+5V_LED` | wyjście boosta → pasek |
| `LED_EN_G` | bramka Q1 |
| `GND` | masa |

## Schemat ideowy

```
                 ┌───────────── R8 100k ─────────────┐
                 │                                    │
 VBAT ─────S│Q1│G├── LED_EN_G ── Rg 100R ── GPIO27 (LED_EN)
  (AO3415A)  │
            D
            │
        BOOST_IN ──┬──── L1 2.2µH ────┐
            │      │               ┌──┴─ SW ─┐
            │    Cin 10µF          │ TPS613222A         +5V_LED
           EN     │                │   U2    VOUT ─┬──────┬────────> WS2812B VDD (×12)
            │    GND               │   GND         │      │
            └─VIN─┘                │   NC(nc)    Cout 22µF C1 470µF
                                   └──┬──          │      │
       GND ─────────────────────────┴─────────────┴──────┴────────> WS2812B GND
```

## Połączenia (net-po-necie)

**Load-switch na wejściu (Q1, high-side P-FET, aktywne LOW):**
- Q1: **S → `VBAT`**, **D → `BOOST_IN`**, **G → `LED_EN_G`**
- R8 (100k): `LED_EN_G` ↔ `VBAT` (pull-up → OFF przy Hi-Z: sen/boot)
- Rg (100R): `GPIO27` ↔ `LED_EN_G`
- Logika: GPIO27 **LOW** → Q1 on → boost zasilony · GPIO27 **Hi-Z** → Vgs=0 → Q1 off → boost całkiem martwy

**Boost (U2 = TPS613222A):**
- **VIN → `BOOST_IN`**
- **EN → `BOOST_IN`** (na stałe on, gdy jest zasilanie)
- **L(SW) → L1**, drugi koniec **L1 → `BOOST_IN`**
- **VOUT → `+5V_LED`**
- **GND → `GND`**
- **NC → niepodłączony**
- Cin 10 µF: `BOOST_IN` ↔ `GND` (przy VIN)
- Cout 22 µF: `+5V_LED` ↔ `GND` (**tuż przy VOUT** — dla pętli)

**Wyjście / pasek:**
- **C1 470 µF**: `+5V_LED` ↔ `GND` (**przy pasku** — rezerwuar anti-pisk)
- `+5V_LED` → VDD 12× WS2812B
- `GPIO14` → DIN (bez zmian)
- wspólny `GND`

## Firmware (`esp32/`)

- **Logika bez zmian** — `LED_EN` (GPIO27) LOW=on / Hi-Z=off działa identycznie;
  `ledPowerOff()` gasi teraz cały boost zamiast samego paska
- `leds.cpp:298`: dodać **`delay(3)`** po `digitalWrite(LED_EN, LOW)` — soft-start
  boosta z zimna (dojście do 5 V + ładowanie 470 µF ≈ do 5 ms)
- `zbox_config.h:19`: poprawić komentarz (pin gasi boost, nie sam pasek)

## Docs

- `docs/hardware.md` — sekcja power path: nowy boost TPS613222A, `C1 470 µF` zostaje
  jako anti-pisk, `LED_EN` teraz na wejściu boosta

## Uwagi

- Dokładne **numery pinów 1–6** U2 wziąć z tabeli „Pin Configuration" datasheetu
  TPS613222A przy tworzeniu symbolu KiCad (tu opisane po nazwach — jednoznacznie)
- Duży `C1 470 µF` na wyjściu = boost musi go naładować przy starcie
  (`t ≈ C·V/I = 470µF·5V/0,5A ≈ 5 ms`); soft-start to udźwignie, gra ze zwłoką
  `delay(3)` w `ledInit()`. Gdyby był hiccup przy starcie → zejść do 220 µF + ceramik
- Star-ground (masa paska i NS4168 zbiegają się przy ogniwie) = opcjonalny dodatek
  dławiący pisk u źródła, gdyby sam 470 µF nie wystarczył po zmianie boosta

## Dlaczego tak (skrót decyzji)

- **TPS613222A**: stałe 5 V (nie trzeba potencjometru — zawsze chcemy 3→5 V), SOT-23,
  ~500 mA przy 3 V→5 V = ~4× zapasu nad realnym poborem 12 LED (master brightness
  capowany na 40/255 w firmware → ~115 mA worst-case)
- **P-FET na wejściu, nie EN**: EN=low zostawia body-diode path (VOUT ≈ VBAT, pasek
  cieknie). Odcięcie wejścia zabija boost i pasek naraz. Istniejący drive
  (LOW=on / Hi-Z→pull-up do BAT) daje na wejściu czysty OFF (Vgs=0) — level-shifter
  zbędny
- **C1 zostaje**: to rezerwuar łykający szpilki prądu animacji LED, inaczej NS4168 piszczy
