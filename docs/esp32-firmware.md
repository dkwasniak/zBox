# ESP32 Firmware

Cały firmware to jeden plik: `esp32/src/main.cpp` (~1250 linii).
Build: PlatformIO, framework hybrydowy `arduino, espidf`, board `lolin_d32_pro`.

Pinout → `docs/hardware.md`.

## Stack

**platformio.ini:**
```ini
[env:lolin_d32_pro]
platform = pioarduino/platform-espressif32 (stable)
board = lolin_d32_pro
framework = arduino, espidf
board_build.partitions = custom_4mb_noota.csv
lib_deps =
    arduino-audio-tools      # pschatzmann
    ESP32-A2DP               # pschatzmann
    arduino-libhelix         # pschatzmann (MP3 decoder)
    Adafruit PN532
    ArduinoJson v7
    WiFiManager              # tzapu
    FastLED
```

**Flagi buildu:** `-DBOARD_HAS_PSRAM=1 -mfix-esp32-psram-cache-issue -Os -std=gnu++17`, `CORE_DEBUG_LEVEL=1`.

**Partycje (`custom_4mb_noota.csv`):** brak OTA, cała 4MB flash: 3.7MB app + 256KB NVS. Powód: A2DP + BT classic + libhelix nie mieszczą się w standardowej partycji OTA.

**Kluczowe sdkconfig (`sdkconfig.defaults`):**
- `CONFIG_BT_BLUEDROID_ENABLED=y`, `CONFIG_BT_A2DP_ENABLE=y`, `CONFIG_BT_BLE_ENABLED=n` - tylko BR/EDR classic, BLE wyłączone żeby zaoszczędzić IRAM
- `CONFIG_SPIRAM=y`, `SPIRAM_SPEED=80`, `SPIRAM_USE_MALLOC=y`
- `CONFIG_FATFS_LFN_HEAP=y`, `CONFIG_FATFS_MAX_LFN=255` - długie nazwy plików
- `CONFIG_COMPILER_OPTIMIZATION_SIZE=y`
- `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240`

## Tor audio

```
SD card ──► currentAudioFile.read() ──► EncodedAudioStream
                                              │
                                     MP3DecoderHelix
                                              │
                                         A2DPStream (TX)
                                              │
                                     Bluetooth A2DP ──► JBL GO 2
```

W kodzie:
```cpp
A2DPStream a2dp;
MP3DecoderHelix mp3Decoder;
EncodedAudioStream decoderStream(&a2dp, &mp3Decoder);
File currentAudioFile;
```

`audioLoop()` czyta 512 bajtów z `currentAudioFile` i pisze do `decoderStream.write()`. Decoder wrzuca PCM do `a2dp`, który wysyła do JBL. W test mode plik zapętla się po EOF.

**PCM5102A / I2S nie jest używany.** GPIO25/26 są zajęte przez BTN_C/BTN_D, GPIO27 wolny w rezerwie.

## Kluczowe stałe (main.cpp)

| Stała | Wartość | Opis |
|-------|---------|------|
| `BT_SPEAKER_NAME` | `"JBL GO 2"` | Nazwa urządzenia do sparowania |
| `TEST_AUDIO_MODE` | **`true`** (!) | Omija NFC, loopuje `TEST_SD_FILE` |
| `TEST_SD_FILE` | `/music/9383471d_babajaga.mp3` | |
| `LONG_PRESS_MS` | 2000 | Próg długiego przycisku |
| `DEBOUNCE_MS` | 50 | |
| `NFC_READ_INTERVAL` | 150 ms | Szybsza niż domyślne 300 |
| `NO_TAG_THRESHOLD` | 1 | Ile pustych odczytów = stop playback |
| `AUDIO_BUF_SIZE` | 512 | Bajty na iterację audio loop |
| `JBL_POWER_PRESS_MS` | 500 | Długość pulsu tranzystora |
| `JBL_STATUS_THRESHOLD` | 180 | ADC próg „JBL włączony" |
| `JBL_BOOT_WAIT_MS` | 5000 | Timeout `ensureJblReady()` na reconnect A2DP |
| `BTN_COUNT` | 4 | Liczba przycisków (A/B/C/D) |
| `BT_VOL_STEP` | 5 (%) | |
| `BT_VOL_DEFAULT` | 50 (%) | |
| `SERVER_HOST` | `"<musicbox-server-ip>"` | **Hardcoded IP** - nie mDNS |
| `SERVER_PORT` | 8000 | |

**⚠️ `TEST_AUDIO_MODE = true` w obecnej wersji repo!** W tym trybie `loop()` pomija cały blok NFC (`#if !TEST_AUDIO_MODE`) - figurki są ignorowane i zapętla się testowy MP3. Przed flashowaniem docelowego urządzenia ustaw na `false`.

## Tryby pracy

### 1. Normal mode (default)

Zrównoleglony boot (~150 ms blokującego delay zamiast 5.5s):

```
LED init → GPIO + 4x btnISR (attachInterruptArg) → SD → sprawdź sync_pending w NVS
       │
       │ (JBL pulse HIGH startuje TUTAJ, tło)
       ▼
  NFC.begin() + delay 100ms → getFirmwareVersion → SAMConfig
       │
  loadMappings() z /data/mappings.json
       │
  NFC pre-scan (200ms) → jeśli tag obecny, zakolejkuj playback
       │
  Dopełnij JBL pulse do 500ms → LOW
       │
  loadBtVolume() z NVS
       │
  a2dp.begin(cfg) z auto_reconnect=true, name="JBL GO 2"
       │
  decoderStream.begin()
       │
  loop() czeka na a2dp.source().is_connected()
       │
  Po BT connect: applyBtVolume() + odtwórz pendingPlaybackPath jeśli ustawione
```

Cały czas logowany jako `[T+xxxx] ...` + `[BOOT] Total boot-to-play: N ms`.

**Deferred playback:** jeśli dziecko postawiło figurkę zanim urządzenie się uruchomi, pre-scan wykryje UID, znajdzie plik w `figurineMap`, sprawdzi istnienie na SD, zapisze do `pendingPlaybackPath`. Po połączeniu BT plik startuje natychmiast (bez kolejnego czekania na NFC scan).

### 2. Test mode (`TEST_AUDIO_MODE = true`)

- Pomija NFC w `loop()`
- `pendingPlaybackPath` ustawia na `TEST_SD_FILE`
- Po BT connect odtwarza w pętli (EOF → reopen)
- Użyteczne do debugowania toru audio/BT bez dotykania figurek

### 3. Sync mode

Trigger: oba przyciski przytrzymane 2s (`handleButtons()` wykrywa `a && b` po `LONG_PRESS_MS`).

1. `preferences.putBool("sync_pending", true)` w NVS
2. `ESP.restart()`
3. Po restarcie `setup()` widzi flagę i woła `runSyncMode()` przed inicjalizacją BT
4. `WiFiManager` - jeśli brak zapisanych credentials, tworzy AP `MusicBox-Setup` (timeout 180s). Użytkownik łączy się → wybiera sieć → ESP32 zapisuje i łączy.
5. Test połączenia do `SERVER_HOST:SERVER_PORT`
6. `GET /api/sync` → manifest JSON z listą `figurines` i `tracks`
7. Dla każdego `track.filename` którego brak na SD: `GET /api/stream/file/{filename}` → zapis do `/music/{filename}` na SD, pasek postępu na WS2812B
8. `syncCleanDir("/music", expectedMusic)` - usuwa pliki spoza manifestu
9. Wygeneruj `/data/mappings.json` na SD w formacie:
   ```json
   {"figurines": {"UID:UID:...": {"file": "xxxxxxxx_name.mp3"}}}
   ```
10. `WiFi.disconnect + WIFI_OFF` → `clearSyncFlag()` → `ESP.restart()`

**Dlaczego restart?** A2DP + WiFi + libhelix + buffers nie mieszczą się w RAM jednocześnie. WiFi i BT są rozłączne w czasie życia urządzenia.

### 4. Deep sleep

Trigger: VOL- długi 2s (sam, bez VOL+).

1. `currentAudioFile.close()`
2. `ledShutdownAnim()` (sekwencja fioletowa)
3. `jblPowerOff()` - sprawdza ADC, wciska power tylko jeśli JBL jest ON
4. `esp_sleep_enable_ext0_wakeup(BTN_A, LOW)` - wake na VOL+
5. `esp_deep_sleep_start()`

Po wybudzeniu ESP32 startuje normalnie (`setup()` od zera).

## Logika NFC

- `reinitNfc()` - wywoływane gdy `nfcErrorCount > NFC_ERROR_THRESHOLD` (10 kolejnych błędów czytania)
- `readNfcTag()` - `readPassiveTargetID(PN532_MIFARE_ISO14443A, ..., timeout=100ms)`, format UID: `XX:XX:XX:XX` hex uppercase
- W `loop()` (gdy `!TEST_AUDIO_MODE`) co `NFC_READ_INTERVAL` (150ms):
  - Nowy UID inny niż `lastNfcUid` → `startPlayback(uid)`
  - Brak tagu przez `NO_TAG_THRESHOLD` (1) iteracji → `stopPlayback()`
- `startPlayback()` - szuka w `figurineMap` (std::map UID→filename), sprawdza plik w `/music/`, otwiera, `isPlaying=true`, `ledSetPlaying()`
- Brak mappingu → `ledFlashWarning()` (pomarańczowe miganie)

## Logika przycisków

4 przyciski obsługiwane przez jedną strukturę `Button buttons[BTN_COUNT]` i wspólną ISR `btnISR(void*)` podpiętą przez `attachInterruptArg`. Debounce 50ms w ISR. Obsługa w `handleButtons()` wołane z `loop()`.

**Mapowanie pinów:**

| Button | Pin | RTC? | Pull-up |
|--------|-----|------|---------|
| BTN_A (VOL+) | GPIO32 | ✅ | wewnętrzny |
| BTN_B (VOL-) | GPIO33 | ✅ | wewnętrzny |
| BTN_C | GPIO25 | ✅ | wewnętrzny |
| BTN_D | GPIO26 | ✅ | wewnętrzny |

**Akcje:**

| Akcja | Efekt |
|-------|-------|
| Krótkie BTN_A (VOL+) | `volumeUp()` - BT vol +5%, zapis NVS, LED overlay 1s |
| Krótkie BTN_B (VOL-) | `volumeDown()` - BT vol -5%, zapis NVS, LED overlay 1s |
| Krótkie BTN_C / BTN_D | log do serial, **akcja nieprzypisana (TODO)** |
| Długie BTN_B 2s (sam) | deep sleep |
| BTN_A + BTN_B razem 2s | sync mode (sync_pending → restart) |
| BTN_A w deep sleep | wake (ext0 GPIO32 LOW) |

Głośność stosowana jest **natychmiast na naciśnięcie** (nie na puszczenie), żeby reakcja była szybka. Może się zdarzyć że krótki klik BTN_A i zaraz BTN_B trafi przypadkiem w sync - kod sobie z tym radzi patrząc na `max(buttons[0].pressStart, buttons[1].pressStart)` (oba muszą być jednocześnie wciśnięte przez pełne 2s).

**Deep sleep wake tylko przez BTN_A.** ESP32 classic nie wspiera oficjalnie `ESP_EXT1_WAKEUP_ANY_LOW`, a przyciski są w konfiguracji pull-up→GND. BTN_C/D/długie_BTN_B wymagają że urządzenie jest awake - nie wybudzają z deep sleep. Jeśli chcesz wake na dowolnym z 4, trzeba zmienić okablowanie na pull-down→VCC + `ext1` z `ANY_HIGH`.

**Struktura `Button`:**
```cpp
struct Button {
    uint8_t pin;
    const char* name;
    volatile bool pressed;
    volatile unsigned long lastInterrupt;
    unsigned long pressStart;
    bool longHandled;
};
```

Dodanie nowej akcji dla BTN_C/BTN_D: edytuj switch w `handleButtons()` (case 2/3) lub dopisz warunek długiego przycisku analogicznie do BTN_B → deep sleep.

## Logika głośności (BT AVRCP)

```cpp
a2dp.setVolume(btVolume / 100.0);   // float 0.0-1.0
```

Przechowywanie: `Preferences` (NVS), namespace `musicbox`, key `bt_volume` (int 0-100). Domyślnie 50, zakres 0-100, krok 5.

**Flaga `btVolumeApplied`:** przy starcie `false`. Po pierwszym wykryciu `a2dp.source().is_connected()` → `applyBtVolume()` + deferred playback. Jeśli BT się rozłączy (`is_connected()` wraca na false), flaga resetuje się → przy kolejnym connect znowu apply.

## Logika JBL power

Trzy ścieżki sterowania zasilaniem JBL, każda w innym miejscu kodu:

### 1. Boot / wake z deep sleep (inline w `setup()`)

Puls włączania wykonany **ręcznie, nieblokująco** żeby 500ms pulsu interleave'owało się z inicjalizacją NFC + mappings + pre-scan. Dzięki temu boot nie traci 500ms:

```cpp
bool jblNeedsPower = !isJblOn();       // ADC check
if (jblNeedsPower) digitalWrite(JBL_POWER, HIGH);
// ... NFC init, mappings, pre-scan (~500ms)
if (jblNeedsPower) {
    delay(JBL_POWER_PRESS_MS - elapsed);  // dopełnij
    digitalWrite(JBL_POWER, LOW);
}
```

### 2. Przed deep sleep → `jblPowerOff()`

Blokujące wyłączenie. Sprawdza ADC, puls 500ms tylko jeśli JBL jest ON.

### 3. Przed runtime playback → `ensureJblReady()`

**Naprawia bug auto-power-off.** JBL GO 2 wyłącza się samoczynnie po ~15 minutach bez sygnału. Bez tej funkcji kolejne postawienie figurki otwierałoby plik bez żadnego dźwięku - bo JBL jest off, a BT nie jest connected.

Wywoływane na początku `startPlayback()` po weryfikacji mappingu i istnienia pliku:

```cpp
bool ensureJblReady() {
    bool adcOn = isJblOn();
    bool btConn = a2dp.source().is_connected();

    if (adcOn && btConn) return true;   // fast path

    if (!adcOn) {                       // wciśnij power
        digitalWrite(JBL_POWER, HIGH);
        delay(JBL_POWER_PRESS_MS);
        digitalWrite(JBL_POWER, LOW);
    }

    ledSetWaitBt();
    // Czekaj do JBL_BOOT_WAIT_MS (5s) na A2DP auto_reconnect
    while (!a2dp.source().is_connected()) {
        if (millis() - start > JBL_BOOT_WAIT_MS) return false;
        delay(50);
    }

    applyBtVolume();                    // re-apply po reconnect
    btVolumeApplied = true;
    return true;
}
```

W razie timeoutu (JBL nie wstaje albo BT nie łapie) - `startPlayback()` woła `ledFlashWarning()` i wraca. Kolejne postawienie figurki uruchomi kolejną próbę.

### Pomocnicze

- `isJblOn()` - 5 próbek ADC GPIO34 co 10ms, max > `JBL_STATUS_THRESHOLD` (180). Używane przez wszystkie trzy ścieżki powyżej.
- `jblPressButtonBlocking(pin, durationMs)` - prosty puls HIGH/delay/LOW.
- `JBL_BOOT_WAIT_MS = 5000` - timeout cold boot JBL + A2DP reconnect. Wcześniej zdefiniowany ale nieużywany, teraz gatekeeper w `ensureJblReady()`.

## LED WS2812B - tryby

Osobny task FreeRTOS (`ledTaskFunc`) pinned do core 0, stack 2048B, priority 1. Niezależny od `loop()`.

| Tryb | Animacja | Kiedy |
|------|----------|-------|
| `LED_BOOT` | Niebieski postęp z 3 mignięciami na aktualnym kroku | Boot, 5 kroków: SD/NFC/mappings/JBL/BT |
| `LED_WAIT_BT` | Breathing niebieski (cubicwave8) | Po `a2dp.begin()`, również w `ensureJblReady()` gdy czeka na reconnect |
| `LED_IDLE` | Breathing zielony | BT connected, brak odtwarzania |
| `LED_PLAYING` | Fala cyan (bieżąca "główka" + gradient) | `isPlaying = true` |
| `LED_VOLUME` | Biały pasek 1-5 diod wg % głośności | Overlay 1s po VOL+/- |
| `LED_SYNC_WIFI` | Żółte miganie (~2.5 Hz) | WiFi connection w sync mode |
| `LED_SYNC_PROGRESS` | Niebieski pasek postępu | Pobieranie plików w sync |
| - | `ledFlashResult(true)` - 3x zielony flash | Sync OK |
| - | `ledFlashResult(false)` - 3x czerwony flash | Sync fail / wake fail |
| - | `ledFlashWarning()` - 2x pomarańczowy | Brak mappingu / brak pliku |
| - | `ledShutdownAnim()` - sekwencja fioletowa | Przed deep sleep |

Delay loopa LED: 15ms (płynne animacje) / 80ms (LED_PLAYING) / 400ms (LED_SYNC_WIFI).

## Mappings

Plik: `/data/mappings.json` na karcie SD.

Format:
```json
{
  "figurines": {
    "04:A3:B2:C1:DE:FF:80": {"file": "9383471d_babajaga.mp3"},
    "04:12:34:56:78:9A:BC": {"file": "abc12345_krolewna.mp3"}
  }
}
```

Generowany przez `performSync()` z manifestu serwera. ESP32 ładuje do `std::map<String, String> figurineMap` przy boot.

## NVS (Preferences)

Namespace: `musicbox`

| Klucz | Typ | Opis |
|-------|-----|------|
| `bt_volume` | int | Głośność AVRCP 0-100 |
| `sync_pending` | bool | Flaga „po restarcie uruchom sync mode" |

## Build / flash

```bash
cd esp32

pio run                     # kompilacja
pio run -t upload           # upload (autodetect port)
pio device monitor          # serial monitor, 115200

# Upload + monitor w jednym:
pio run -t upload && pio device monitor
```

Pre-scripts w `platformio.ini`:
- `updatesdkconfig.py` - synchronizuje `sdkconfig.defaults` → `sdkconfig.lolin_d32_pro`
- `generate_certs.py` - generuje certyfikaty jeśli brak

## Znane problemy i pitfalle

1. **`TEST_AUDIO_MODE = true`** zostawione w repo - przed flashowaniem urządzenia docelowego ustaw `false` (main.cpp:66).

2. **`SERVER_HOST = "<musicbox-server-ip>"`** - hardcoded IP. Jeśli zmieni się IP RPi, sync przestanie działać. Docelowo warto przenieść do portalu WiFiManager.

3. **Obsługa rozłączenia BT w trakcie grania jest częściowa** - `audioLoop()` dalej pisze do `decoderStream.write()` (może blokować aż do reconnect), `loop()` resetuje tylko `btVolumeApplied`. Pełne odzyskanie dzieje się dopiero przy kolejnym `startPlayback()` przez `ensureJblReady()`. Jeśli chcesz detekcję „w trakcie gry", dopisz watchdog w `audioLoop()` który wywoła `ensureJblReady()` po N sekundach braku connectu.

4. **`body.concat((char*)buf, got)`** w `httpGet()` (main.cpp:727) - API Arduino String nie ma oficjalnie `concat(ptr, len)`, ale na obecnym core'u działa. Jeśli po update kompilator zgłosi błąd - podmienić na pętlę.

5. **`AUDIO_BUF_SIZE = 512`** - mało, ale A2DP buforuje. Jeśli są trzeszczenia, spróbuj 1024/2048.

6. **Pre-scan NFC a dźwięk uruchomienia** - `#if !TEST_AUDIO_MODE` blok pre-scan jest aktywny tylko w normal mode. W test mode zawsze kolejkuje `TEST_SD_FILE`.

7. **Dźwięki systemowe (`ready.mp3`, `start.mp3`)** - serwer ma endpoint `/api/system_sounds/{sound_name}` i folder `music/system/`, ale **firmware tego nie używa**. To zaszłość z wcześniejszej wersji (online streaming). Jeśli chcesz je przywrócić, trzeba dopisać ich odtwarzanie w `setup()` / `startPlayback()`.

8. **PCM5102A i I2S** - fizycznie na PCB, firmware w ogóle nie inicjalizuje I2S. Jeśli kiedykolwiek chcesz wrócić do toru I2S zamiast BT, trzeba napisać drugi tor audio (np. `I2SStream` z arduino-audio-tools) - obecny `A2DPStream` jest wyłączną ścieżką.

9. **BTN_C / BTN_D bez akcji** - przyciski są w pełni okablowane na poziomie firmware (pinMode, ISR, debounce, timing long-press), ale krótkie naciśnięcie tylko loguje `[BTN] Short press: C/D (no action)`. Do przypisania akcji: zmień `case 2:`/`case 3:` w `handleButtons()` (main.cpp). Długi press dla C/D też nie ma akcji - dopisać analogicznie do BTN_B → deep sleep.

10. **PCB header J7 dalej 4-pinowy** - obecny custom PCB ma pin header dla 2 przycisków + 2x GND. Na 4 fizyczne przyciski potrzebna nowa rewizja PCB z 5- lub 6-pin headerem (4 sygnały + GND), plus ścieżki z GPIO25/26 do nowego headera. Tymczasowo można podłączyć BTN_C/D przewodami bezpośrednio do listew L/P Lolin D32 Pro.
