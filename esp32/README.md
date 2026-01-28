# MusicBox ESP32 Firmware

Firmware dla ESP32 do projektu MusicBox - muzycznego pudełka dla dzieci.

## Wymagania sprzętowe

| Komponent | Model |
|-----------|-------|
| Mikrokontroler | ESP32 WROOM32 |
| Czytnik NFC | PN532 (tryb SPI) |
| DAC | PCM5102A |
| Głośnik | JBL Go (przez AUX) |
| Przyciski | 2x tact switch |

## Okablowanie

### PN532 (NFC) → ESP32 (SPI)
| PN532 | ESP32 |
|-------|-------|
| VCC   | 3.3V  |
| GND   | GND   |
| SCK   | GPIO18 |
| MISO  | GPIO19 |
| MOSI  | GPIO23 |
| SS    | GPIO5  |

**Ważne:** Ustaw przełączniki na PN532 w tryb SPI (zazwyczaj: SEL0=OFF, SEL1=ON)

### PCM5102A (DAC) → ESP32 (I2S)
| PCM5102A | ESP32 | Uwagi |
|----------|-------|-------|
| VIN      | 5V    | Zasilanie |
| GND      | GND   | Masa cyfrowa |
| BCK      | GPIO26 | Bit Clock |
| LRCK     | GPIO25 | Left/Right Clock |
| DIN      | GPIO27 | Data In |
| SCK      | GND    | System Clock (ustawia tryb pracy) |

**Pozostałe piny** (FLT, DEMP, XSMT, FMT, AGND) - niepodłączone

**Audio:** Kabel mini jack 3.5mm z gniazda na płytce PCM5102A → wejście AUX w JBL Go

### Przyciski
| Przycisk | ESP32 | Drugi pin |
|----------|-------|-----------|
| VOL+     | GPIO32 | GND |
| VOL-     | GPIO33 | GND |

### LED
Wykorzystywana jest wbudowana dioda LED na GPIO2.

## Instalacja

### 1. Zainstaluj PlatformIO

```bash
# Przez pip
pip install platformio

# Lub przez VSCode - zainstaluj rozszerzenie "PlatformIO IDE"
```

### 2. Sklonuj/skopiuj projekt

```bash
cd musicbox/esp32
```

### 3. Kompilacja

```bash
pio run
```

### 4. Upload do ESP32

```bash
pio run -t upload
```

### 5. Monitor szeregowy

```bash
pio device monitor
```

## Pierwsze uruchomienie - konfiguracja WiFi

1. Po pierwszym uruchomieniu ESP32 utworzy sieć WiFi: **MusicBox-Setup**
2. Połącz się z tą siecią telefonem lub laptopem
3. Powinien automatycznie otworzyć się portal konfiguracyjny
4. Jeśli nie - wejdź na adres `192.168.4.1`
5. Wybierz swoją sieć WiFi i podaj hasło
6. ESP32 zrestartuje się i połączy z Twoją siecią

**Reset ustawień WiFi:** Jeśli chcesz zmienić sieć WiFi, sflashuj ponownie firmware.

## Obsługa

### Przyciski
| Akcja | Efekt |
|-------|-------|
| Krótkie VOL+ | Głośność +1 |
| Krótkie VOL- | Głośność -1 |
| Przytrzymanie VOL- (2s) | Deep sleep |
| Wciśnięcie VOL+ (w sleep) | Wybudzenie |

### LED
| Stan LED | Znaczenie |
|----------|-----------|
| Świeci ciągle | Urządzenie działa |
| Szybkie miganie | Tryb konfiguracji WiFi |
| Wyłączona | Deep sleep |
| 10x szybkie mignięcia | Błąd NFC |
| 2x wolne mignięcia | Gotowe do pracy |

### NFC
- Postaw figurkę z tagiem NFC → muzyka zaczyna grać
- Zdejmij figurkę → muzyka się zatrzymuje

### Dźwięki systemowe
ESP32 odtwarza ciche dźwięki ambientowe w dwóch sytuacjach:
- **ready.mp3** - Po uruchomieniu, gdy urządzenie jest gotowe do pracy
- **start.mp3** - Przy wykryciu tagu NFC, tuż przed rozpoczęciem muzyki

Pliki muszą znajdować się w `music/system/` na serwerze. Głośność: 3/21 (ciche).

## Rozwiązywanie problemów

### "PN532 not found!"
- Sprawdź okablowanie SPI
- Upewnij się, że przełączniki na PN532 są ustawione na SPI
- Sprawdź zasilanie (3.3V)

### Brak dźwięku
- Sprawdź okablowanie I2S
- Upewnij się, że PCM5102A ma 5V na VIN
- Sprawdź połączenie SCK → GND na PCM5102A
- Sprawdź kabel AUX do głośnika

### "Could not resolve server address"
- Upewnij się, że serwer MusicBox działa na Raspberry Pi
- Sprawdź czy ESP32 i RPi są w tej samej sieci
- Spróbuj użyć stałego IP zamiast mDNS (edytuj `SERVER_HOST` w kodzie)

### WiFi nie łączy się
- Trzymaj ESP32 bliżej routera
- Upewnij się, że sieć to 2.4GHz (ESP32 nie obsługuje 5GHz)
- Sflashuj ponownie aby zresetować ustawienia WiFi

### Dźwięki systemowe się nie odtwarzają
- Sprawdź czy pliki `ready.mp3` i `start.mp3` istnieją w folderze `music/system/` na serwerze
- Sprawdź logi ESP32 (serial monitor) - powinny pokazać błąd 404 jeśli plików brak
- Dźwięki są opcjonalne - ich brak nie blokuje działania głównego odtwarzania

## Konfiguracja

Główne parametry w `src/main.cpp`:

```cpp
#define SERVER_HOST       "<musicbox-server>"  // Adres serwera
#define SERVER_PORT       8000            // Port serwera
#define WIFI_AP_NAME      "MusicBox-Setup" // Nazwa hotspotu
#define VOLUME_DEFAULT    10              // Domyślna głośność (0-21)
#define LONG_PRESS_MS     2000            // Czas dla deep sleep (ms)
```

## Struktura projektu

```
esp32/
├── platformio.ini    # Konfiguracja PlatformIO
├── src/
│   └── main.cpp      # Główny kod firmware
├── include/          # Nagłówki (opcjonalne)
├── lib/              # Lokalne biblioteki (opcjonalne)
└── README.md         # Ten plik
```
