# Hardware MusicBox

## Komponenty

- **ESP32 Lolin D32 Pro** (WROOM32 + PSRAM + wbudowany slot SD)
- **PN532** - czytnik NFC (Software SPI)
- **JBL GO 2** - głośnik przez **Bluetooth A2DP** (sparowany, sterowany przez BT AVRCP + tranzystor do włączania)
- **WS2812B** - pasek 12 diod RGB
- **4x tact switch** - BTN_A (VOL+), BTN_B (VOL-), BTN_C i BTN_D (rezerwa, akcje TODO)
- **1x tranzystor NPN BC547** + rezystor 2.2kΩ - sterowanie przyciskiem POWER JBL
- **Karta SD** - muzyka i mappingi offline
- **PCM5102A DAC** - *obecny na PCB, ale NIEUŻYWANY w aktualnym firmware* (audio idzie przez Bluetooth)

## Schemat blokowy

```
┌─────────────┐  Bluetooth A2DP  ┌─────────────┐
│ ESP32 Lolin │ ───────────────► │   JBL Go 2  │
│  D32 Pro    │                  │ (BT speaker)│
└─────────────┘                  └─────────────┘
       │                                ▲
       │ SW SPI (NFC)                   │ GPIO13 przez tranzystor NPN
       ▼                                │ (tylko POWER - VOL przez AVRCP)
┌─────────────┐                 ┌─────────────────┐
│   PN532     │                 │ JBL PCB         │
│   (NFC)     │                 │ (przycisk POWER)│
└─────────────┘                 └─────────────────┘
```

> PCM5102A + kabel AUX to zaszłość z wcześniejszej wersji. Jeśli planujesz wrócić do I2S, trzeba napisać oddzielny tor audio w firmware - obecny używa wyłącznie A2DP TX.

## Kompletna mapa GPIO ESP32 Lolin D32 Pro

| GPIO | Funkcja | Typ | Uwagi |
|------|---------|-----|-------|
| 4 | SD CS | Output (HW SPI) | Wbudowany slot, nie zmieniać |
| 5 | PN532 SS | Output (SW SPI) | Strapping pin - pull-up OK |
| 12 | PN532 MOSI | Output (SW SPI) | Strapping pin - musi być LOW przy boot (PN532 nie ciągnie HIGH) |
| 13 | JBL POWER | Output | Tranzystor NPN, 2.2kΩ na bazie |
| 14 | LED WS2812B DIN | Output | 3.3V logic, OK na krótkim kablu |
| 18 | SD SCK | HW SPI (wewnętrzny) | ZAJĘTY - nie używać! |
| 19 | SD MISO | HW SPI (wewnętrzny) | ZAJĘTY - nie używać! |
| 21 | PN532 MISO | Input (SW SPI) | Domyślny I2C SDA - I2C niedostępny |
| 22 | PN532 SCK | Output (SW SPI) | Domyślny I2C SCL - I2C niedostępny |
| 23 | SD MOSI | HW SPI (wewnętrzny) | ZAJĘTY - nie używać! |
| 25 | BTN_C | Input (pull-up) | RTC, wolny po rezygnacji z PCM5102A |
| 26 | BTN_D | Input (pull-up) | RTC, wolny po rezygnacji z PCM5102A |
| 27 | LED_EN | Output | LOW = LEDy ON, HIGH = LEDy OFF (steruje Q2 P-MOSFET) |
| 32 | BTN_A (VOL+) | Input (pull-up) | RTC, wake-up z deep sleep przez ext0 |
| 33 | BTN_B (VOL-) | Input (pull-up) | RTC |
| 34 | JBL STATUS | Input (ADC) | Tylko input, dzielnik 10kΩ/22kΩ |

**Wolne GPIO do rozbudowy:** 0\*, 2\*, 15\*, 17, 35\*, 36\*, 39\*
(\*) z ograniczeniami: 0/2 = strapping pins, 15 = strapping pin (pull-down 10kΩ wymagany), 35/36/39 = tylko input (brak pull-up)

## Okablowanie PN532 → ESP32 (Software SPI)

Software SPI, żeby nie kolidować z HW SPI karty SD (która zajmuje 18/19/23).

| PN532 | ESP32 | Uwagi |
|-------|-------|-------|
| VCC   | 3.3V  | |
| GND   | GND   | |
| SCK   | GPIO22 | |
| MISO  | GPIO21 | |
| MOSI  | GPIO0 |  |
| SS    | GPIO5  | |

PN532 musi być w trybie **SPI** (przełączniki SEL0/SEL1 na module).

## Okablowanie przycisków

Cztery przyciski tact switch, wszystkie łączone z GND, z wewnętrznym pull-up ESP32.

| Przycisk | ESP32 | Drugi pin | Rola |
|----------|-------|-----------|------|
| BTN_A (VOL+) | GPIO32 | GND | krótki: BT volume +5%, wake z deep sleep (ext0) |
| BTN_B (VOL-) | GPIO33 | GND | krótki: BT volume -5%, długi 2s (sam): deep sleep |
| BTN_C | GPIO25 | GND | TODO - akcja nieprzypisana |
| BTN_D | GPIO26 | GND | TODO - akcja nieprzypisana |
| BTN_A + BTN_B | 32 + 33 | - | oba trzymane 2s: sync mode |

Wszystkie cztery GPIO są RTC-capable, ale w obecnym firmware wake-up z deep sleep jest tylko przez **ext0 na BTN_A (GPIO32)**. ESP32 classic nie wspiera oficjalnie `ESP_EXT1_WAKEUP_ANY_LOW`, a wszystkie przyciski są konfiguracji pull-up→GND. Jeśli w przyszłości potrzebujesz żeby dowolny z 4 przycisków wybudzał, trzeba zamienić okablowanie na pull-down→VCC i użyć `ext1` z `ANY_HIGH`.

Wewnętrzny pull-up ESP32 wystarcza, zewnętrzne rezystory nie są potrzebne.

## WS2812B - pasek 12 diod RGB

| WS2812B | ESP32/Zasilanie | Uwagi |
|---------|-----------------|-------|
| VCC | 5V (z MT3608) | Zasilanie przez step-up, sterowane GPIO27 |
| GND | GND | Star ground |
| DIN | GPIO14 | 3.3V logic, OK na krótkim kablu |

**Filtrowanie szumów:** kondensator elektrolityczny **1000µF / 25V** na wejściu MT3608 (VIN→GND) eliminuje pisk w głośniku BT spowodowany modulacją prądu przez animacje LED.

### Zasilanie LEDów (Q2 + MT3608)

LEDy WS2812B wymagają 5V. Zasilanie jest podawane przez step-up MT3608 (U2), który jest włączany/wyłączany P-MOSFETem AO3415A (Q2) sterowanym z GPIO27 (LED_EN).

```
GPIO27 (LED_EN)
    │
    ├── R8 (100kΩ pull-up do VIN) ← domyślnie Gate = HIGH = OFF
    │
    └── Q2 Gate (AO3401 P-MOSFET)
         Source ← VIN (bateria/zasilanie)
         Drain  → U2 VIN (MT3608 step-up)
                          │
                       VOUT → 5V → WS2812B VCC
```

| Stan GPIO27 | Q2 | MT3608 | LEDy |
|-------------|-----|--------|------|
| LOW | ON | zasilany | świecą |
| HIGH / floating | OFF | odcięty | wyłączone |

R8 (100kΩ) zapewnia, że LEDy są domyślnie wyłączone (przy boot / deep sleep / reset GPIO jest w stanie Hi-Z).

Animacje w firmware: boot progress, wait-for-BT, idle, playing, volume, sync wifi, sync progress, success/error flash, shutdown. Szczegóły: `docs/esp32-firmware.md`.

## Sterowanie JBL Go (tranzystor NPN BC547)

Przycisk POWER JBL jest normalnie otwarty (NO). Tranzystor zwiera nóżki przycisku gdy GPIO=HIGH.

| Funkcja | GPIO | Podłączenie tranzystora | Rezystor bazowy |
|---------|------|--------------------------|-----------------|
| JBL POWER | GPIO13 | Collector→lewa nóżka POWER (~4V), Emitter→prawa nóżka (GND) | 2.2kΩ Base→GPIO13 |

**Głośność jest sterowana przez Bluetooth AVRCP, nie przez tranzystory** - dlatego VOL+/VOL- JBL nie mają tranzystorów (usunięte Q2, Q3, R4, R5 na PCB).

**WAŻNE:** GND ESP32 musi być połączony z GND JBL (minus baterii na płytce JBL).

## Odczyt statusu JBL (czy włączony)

Dzielnik napięcia z linii statusowej JBL na ADC ESP32 (GPIO34 - input only, bez pull-up).

| Z | Do | Uwagi |
|---|-----|-------|
| Linia statusowa JBL (~4V gdy włączony) | rezystor 10kΩ → GPIO34 | |
| GPIO34 | rezystor 22kΩ → GND | Daje ~2.75V na ADC (bezpieczne dla ESP32) |

Próg ADC w firmware: `JBL_STATUS_THRESHOLD = 180` (main.cpp).

Używane do:
- **Boot inline w `setup()`** - puls power tylko jeśli ADC mówi że JBL jest OFF, puls interleavowany z NFC init (nieblokujący)
- **`ensureJblReady()`** (runtime, przed każdym `startPlayback()`) - handles auto-power-off JBL po bezczynności, pulsuje power i czeka na A2DP reconnect
- **`jblPowerOff()`** (przed deep sleep) - nie wciska power jeśli JBL już wyłączony

## Karta SD (wbudowany slot Lolin D32 Pro)

CS=GPIO4. W firmware: `SPI.begin(18, 19, 23, 4)` + `SD.begin(4)`.

GPIO18/19/23 są wewnętrznie połączone ze slotem SD (routing na PCB). Choć wyprowadzone na pin headers, **NIE WOLNO ich używać** do innych celów gdy SD jest aktywna (konflikty na magistrali SPI). Jedyny wyjątek: dodatkowy slave SPI z osobnym CS.

Długie nazwy plików (polskie znaki, spacje) włączone w `sdkconfig.defaults` (`CONFIG_FATFS_LFN_HEAP=y`).

## Zasilanie

Zewnętrzny zasilacz 5V ze **star ground**. Wspólne dla ESP32, WS2812B i JBL Go.
Nie zasilać z USB ESP32 (za mało prądu przy pracy WS2812B + BT + peak).

## Custom PCB

- Wejście zasilania USB-C (J1) z rezystorami pull-up 5.1kΩ (R1, R2) na CC1/CC2
- Złącze JST-PH 2-pin (J2) na akumulator LiPo (pad do wlutowania baterii - pin 1 = BAT+ do pinu BAT Lolina, pin 2 = GND). Ładowanie obsługuje wbudowana ładowarka TP4054 na Lolin D32 Pro (ładowanie z USB-C)
- Pin header PN532 (J3, 6-pin)
- Pin header PCM5102A (J4, 5-pin) - *nieużywany w obecnym firmware*
- Pin header przycisków (J7, 4-pin) - *obecnie tylko BTN_A/BTN_B; BTN_C/BTN_D wymagają nowej rewizji PCB albo tymczasowego podłączenia przewodami do listew L/P Lolina (GPIO25/26)*
- Pin header JBL (J10, 4-pin: POWER/STATUS/GND/VCC)
- Pin header LED (J11, 3-pin: DIN/VCC/GND)
- Gniazda ESP32 Lolin D32 Pro (J8 lewy, J9 prawy, 16 pinów każdy)
- Tranzystor BC547 (Q1) + rezystor 2.2kΩ (R3) - tylko JBL POWER
- Rezystor 10kΩ (R6) na linii JBL_STATUS (część dzielnika napięcia)
- Pull-down 10kΩ na GPIO15 (strapping pin wymaga stabilnego LOW przy boot)

**Znane rzeczy do poprawy w następnej rewizji PCB:**
- Złącze J4 (PCM5102A) ma 5 pinów - brakuje SCK. Dla obecnego firmware bez znaczenia (PCM5102A nie jest używany), ale dla kompletności można dodać 6-pin albo w ogóle usunąć jeśli I2S nie wraca.
- Header przycisków J7 jest 4-pinowy (BTN_A + BTN_B + 2x GND). Firmware obsługuje 4 przyciski (BTN_A/B/C/D na GPIO32/33/25/26) - do nowej rewizji warto poszerzyć J7 do 5-pin (4 sygnały + wspólny GND) albo 6-pin. Obecnie BTN_C/D można podłączyć tylko przewodami bezpośrednio do listew L/P Lolin D32 Pro.
- Q2, Q3, R4, R5 (tranzystory VOL+/VOL- JBL) zostały usunięte z BOM - głośność sterowana przez AVRCP.
- Q2 (LED_EN) zmieniony z BS250 na AO3415A (BS250 230mA za mało dla 5x WS2812B ~300mA full white).
