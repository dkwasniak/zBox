Plan: Rozbicie esp32/src/main.cpp na moduły                                                                                                                                                                                                                     
                                                                                                                                                                                                                                                               
 Context                                                                                                                                                                                                                                                       
                                     
 esp32/src/main.cpp ma 2515 linii i zawiera cały firmware: LED, NFC, audio, przyciski, BT, sync, sleep, bateria. Cel: modułowa struktura z osobnymi parami .h/.cpp, zachowując identyczne zachowanie sprzętowe (zero zmian timing/ISR/semantyki).

 Zasady z embedded-refactoring skill:
 - Jeden moduł = jeden commit + weryfikacja na HW przed następnym krokiem
 - Kolejność init w setup() pozostaje niezmieniona i widoczna
 - volatile, critical sections, IRAM_ATTR — bez dotykania
 - Mierzyć baseline (HWM, heap, bss/text) przed i po każdym większym kroku

 ---
 Docelowa struktura plików

 esp32/src/
 ├── main.cpp                (~250 linii: tylko setup() + loop())
 ├── musicbox_config.h       (wszystkie #define: piny, stałe, timeouty, BTN_COUNT)
 ├── logging.h               (#define LOG/LOGLN — runtime makra, osobno od konfiguracji)
 ├── helpers.h               (inline pure functions: urlEncode, uidToString, batteryBars)
 ├── shared_types.h          (Button struct, NfcEvent struct — TYLKO te dwa)
 ├── state.h / state.cpp     (minimalne shared globals z extern deklaracjami)
 ├── leds.h / leds.cpp       (LED task, animacje, API: ledSetXxx, ledSuspendTask, ledResumeTask)
 ├── jbl.h / jbl.cpp         (isJblOn, jblPowerOff, jblPressButtonBlocking)
 ├── battery.h / battery.cpp (readBatteryVoltage — tylko hardware ADC read)
 ├── volume.h / volume.cpp   (NVS, volumeUp/Down — woła audioSetBtVolumePercent)
 ├── sd_storage.h/.cpp       (initSD, loadMappings, readJsonFromSd)
 ├── audio.h / audio.cpp     (audioInit, audioStartFile, audioStop, audioIsRunning,
 │                            audioSetBtVolumePercent, audioBtIsConnected — a2dp ukryte,
 │                            AudioCmd/AudioCmdType prywatne w audio.cpp)
 ├── nfc_module.h/.cpp       (nfcInit, nfcStartTask, nfcStopTaskForSleep, nfcPowerDown,
 │                            nfcGetEvent — kolejka ukryta)
 ├── buttons_isr.h/.cpp      (btnISR IRAM_ATTR, buttonsInit; buttons[] jako extern)
 ├── playback.h/.cpp         (startPlayback, stopPlayback, ensureJblReady)
 ├── sleep.h / sleep.cpp     (enterDeepSleep, handleWakeFromDeepSleep)
 ├── buttons.h / buttons.cpp (handleButtons — logika akcji)
 ├── sync_mode.h/.cpp        (runSyncMode, performSync, syncDownloadFile)
 └── persistent_log.h/.cpp   (bez zmian — ma własny PLOGF, nie mieszać z LOG)

 Noty:
 - musicbox_config.h zamiast config.h — unika kolizji z lib/ESP32-A2DP/src/config.h
 - logging.h osobny od musicbox_config.h — piny/stałe nie mieszają się z makrami runtime
 - persistent_log.h ma własne PLOGF — logging.h (LOG/LOGLN) to odrębny mechanizm

 ---
 Podział state — zasada ukrywania za API

 ┌──────────────────────────────┬─────────────────────────────────────────────────────┐
 │    Zamiast eksportować...    │                   Eksponujemy API                   │
 ├──────────────────────────────┼─────────────────────────────────────────────────────┤
 │ QueueHandle_t audioQueue     │ audioStartFile(path), audioStop(), audioIsRunning() │
 ├──────────────────────────────┼─────────────────────────────────────────────────────┤
 │ A2DPStream a2dp              │ audioSetBtVolumePercent(int), audioBtIsConnected()  │
 ├──────────────────────────────┼─────────────────────────────────────────────────────┤
 │ QueueHandle_t nfcQueue       │ nfcGetEvent(NfcEvent *e, TickType_t timeout)        │
 ├──────────────────────────────┼─────────────────────────────────────────────────────┤
 │ TaskHandle_t audioTaskHandle │ audioDeleteTaskForSleep()                           │
 ├──────────────────────────────┼─────────────────────────────────────────────────────┤
 │ TaskHandle_t nfcTaskHandle   │ nfcStopTaskForSleep()                               │
 ├──────────────────────────────┼─────────────────────────────────────────────────────┤
 │ TaskHandle_t ledTaskHandle   │ ledSuspendTask(), ledResumeTask()                   │
 ├──────────────────────────────┼─────────────────────────────────────────────────────┤
 │ volatile LedMode ledMode     │ ledSetXxx() — jedyny interfejs LED                  │
 └──────────────────────────────┴─────────────────────────────────────────────────────┘

 logging.h:
 #pragma once
 #include <Arduino.h>
 #define LOG(fmt, ...) Serial.printf("<%lu> " fmt, millis(), ##__VA_ARGS__)
 #define LOGLN(msg) LOG(msg "\n")

 shared_types.h (TYLKO dwa typy — AudioCmdType prywatny w audio.cpp):
 #pragma once
 #include <stdint.h>
 #include <stdbool.h>

 struct Button {
     uint8_t pin;
     const char *name;
     volatile bool pressed;
     volatile unsigned long lastInterrupt;
     unsigned long pressStart;
     bool longHandled;
 };

 struct NfcEvent {
     bool tagPresent;
     char uid[30];
 };

 state.h (wyłącznie prawdziwie shared globals):
 #pragma once
 #include <Arduino.h>
 #include <map>
 #include "musicbox_config.h"

 extern volatile bool g_btConnected;
 extern bool btVolumeApplied;
 extern unsigned long btWaitStart;   // unsigned long (nie bool!)
 extern bool jblRecoveryDone;
 extern bool sdReady;
 extern std::map<String, String> figurineMap;
 extern std::map<String, String> systemSoundMap;
 extern volatile char lastNfcUid[30];
 extern volatile bool isPlaying;     // kanoniczne źródło prawdy
 extern volatile bool trackEndedFlag;
 extern String pendingPlaybackPath;
 extern String pendingPlaybackUid;
 extern bool nfcReady;
 extern unsigned long bootStart;
 extern bool bootTimingDone;
 extern unsigned long lastActivityMs;

 static w module (tylko jeden właściciel):
 - leds.cpp: leds[], fastLedInitialized, animSteps, ledTaskHandle_
 - audio.cpp: mp3Decoder, decoderStream, audioInfoLogger, audioQueue, audioTaskHandle, a2dp, AudioCmd struct, AudioCmdType enum
 - nfc_module.cpp: nfc object, nfcMutex, RTC_DATA_ATTR rtcNfcPowerDownSent, nfcErrorCount, nfcQueue, nfcTaskHandle
 - volume.cpp: btVolume, Preferences preferences
 - sync_mode.cpp: syncServerIP

 isPlaying vs audioIsRunning(): isPlaying w state.h = kanoniczne źródło. audioIsRunning() to cienki wrapper (return isPlaying;).

 ---
 Kolejność ekstrakcji

 ┌──────┬───────────────────────────────────────┬─────────────────────────────────────────┬───────────────────────────────────────────────────┐
 │ Krok │                 Moduł                 │       Zakres linii (przybliżony)        │                      Commit                       │
 ├──────┼───────────────────────────────────────┼─────────────────────────────────────────┼───────────────────────────────────────────────────┤
 │ 0a   │ musicbox_config.h                     │ 51–114 (#define, stałe, BTN_COUNT)      │ refactor: extract musicbox_config.h               │
 ├──────┼───────────────────────────────────────┼─────────────────────────────────────────┼───────────────────────────────────────────────────┤
 │ 0b   │ logging.h + helpers.h                 │ LOG/LOGLN + pure functions              │ refactor: extract logging.h and helpers.h         │
 ├──────┼───────────────────────────────────────┼─────────────────────────────────────────┼───────────────────────────────────────────────────┤
 │ 0c   │ shared_types.h + state.h/cpp skeleton │ Button, NfcEvent + shared globals       │ refactor: extract shared_types and state skeleton │
 ├──────┼───────────────────────────────────────┼─────────────────────────────────────────┼───────────────────────────────────────────────────┤
 │ 1    │ battery                               │ 1994–2017 (readBatteryVoltage only)     │ refactor: extract battery module                  │
 ├──────┼───────────────────────────────────────┼─────────────────────────────────────────┼───────────────────────────────────────────────────┤
 │ 2    │ leds                                  │ 299–612 (ledTaskHandle ukryty)          │ refactor: extract leds module                     │
 ├──────┼───────────────────────────────────────┼─────────────────────────────────────────┼───────────────────────────────────────────────────┤
 │ 3    │ jbl                                   │ 617–653                                 │ refactor: extract jbl module                      │
 ├──────┼───────────────────────────────────────┼─────────────────────────────────────────┼───────────────────────────────────────────────────┤
 │ 4    │ volume                                │ 659–703                                 │ refactor: extract volume module                   │
 ├──────┼───────────────────────────────────────┼─────────────────────────────────────────┼───────────────────────────────────────────────────┤
 │ 5    │ sd_storage                            │ 709–779                                 │ refactor: extract sd_storage module               │
 ├──────┼───────────────────────────────────────┼─────────────────────────────────────────┼───────────────────────────────────────────────────┤
 │ 6    │ audio                                 │ 785–878 (a2dp + kolejka ukryte)         │ refactor: extract audio module                    │
 ├──────┼───────────────────────────────────────┼─────────────────────────────────────────┼───────────────────────────────────────────────────┤
 │ 7    │ nfc_module                            │ 884–1160 + nfcPowerDown                 │ refactor: extract nfc_module                      │
 ├──────┼───────────────────────────────────────┼─────────────────────────────────────────┼───────────────────────────────────────────────────┤
 │ 8    │ buttons_isr                           │ btnISR + buttonsInit (buttons[] extern) │ refactor: extract buttons_isr (ISR + init only)   │
 ├──────┼───────────────────────────────────────┼─────────────────────────────────────────┼───────────────────────────────────────────────────┤
 │ 9    │ playback                              │ 1171–1276                               │ refactor: extract playback module                 │
 ├──────┼───────────────────────────────────────┼─────────────────────────────────────────┼───────────────────────────────────────────────────┤
 │ 10   │ sleep                                 │ 1278–1442                               │ refactor: extract sleep module                    │
 ├──────┼───────────────────────────────────────┼─────────────────────────────────────────┼───────────────────────────────────────────────────┤
 │ 11   │ buttons                               │ handleButtons()                         │ refactor: extract buttons action handler          │
 ├──────┼───────────────────────────────────────┼─────────────────────────────────────────┼───────────────────────────────────────────────────┤
 │ 12   │ sync_mode                             │ 1448–1988                               │ refactor: extract sync_mode module                │
 ├──────┼───────────────────────────────────────┼─────────────────────────────────────────┼───────────────────────────────────────────────────┤
 │ 13   │ cleanup main.cpp                      │ setup() + loop()                        │ refactor: cleanup main.cpp to ~250 lines          │
 └──────┴───────────────────────────────────────┴─────────────────────────────────────────┴───────────────────────────────────────────────────┘

 Łącznie: 14 commitów.

 ---
 Szczegóły krytycznych kroków

 Krok 0b — logging.h + helpers.h

 batteryBars() w helpers.h — używany przez buttons.cpp, NIE przez battery.cpp.

 helpers.h includes:
 #pragma once
 #include <Arduino.h>   // String
 #include <ctype.h>     // isalnum() dla urlEncode()

 Aktualizacje testów:
 - test_uid_format.cpp: usunąć inline kopię uidToString(), dodać #include "helpers.h"
 - test_volume.cpp: usunąć inline kopię batteryBars(), dodać #include "helpers.h"
 - Weryfikacja: pio test -e native — 31 testów zielonych

 Krok 2 — leds

 handleWakeFromDeepSleep() woła FastLED przed FreeRTOS scheduler.
 leds.h eksponuje:
 void ledPreInitHardware();  // tylko pinMode/digitalWrite/FastLED — ZERO FreeRTOS
 void ledSuspendTask();      // vTaskSuspend(ledTaskHandle_)
 void ledResumeTask();       // vTaskResume(ledTaskHandle_)

 Krok 6 — audio

 a2dp, AudioCmd, AudioCmdType — prywatne w audio.cpp.

 audioInit() przenosi CAŁY blok A2DP z setup() — kopiując dokładne wywołania z kodu, bez zmiany receivera/metody:
 // Z setup() → do audioInit() (przykład — sprawdzić dokładny kod w main.cpp):
 a2dp.defaultConfig(TX_MODE);
 cfg.name = BT_SPEAKER_NAME;      // UWAGA: BT_SPEAKER_NAME (nie BT_DEVICE_NAME)
 cfg.auto_reconnect = true;
 a2dp.source().set_avrc_rn_events({});
 a2dp.source().set_on_connection_state_changed(onBtStateChange);
 a2dp.begin(cfg);
 delay(100);
 g_btConnected = a2dp.source().is_connected();
 decoderStream.begin(...);
 mp3Decoder.addNotifyAudioChange(audioInfoLogger);
 audioQueue = xQueueCreate(...);
 xTaskCreatePinnedToCore(audioTaskFunc, ...);
 onBtStateChange — zdefiniowany jako static funkcja w audio.cpp, rejestrowany wewnątrz audioInit().

 Publiczny interfejs audio.h:
 void audioInit();
 void audioStartFile(const char *path);
 void audioStop();
 bool audioIsRunning();
 void audioDeleteTaskForSleep();
 void audioSetBtVolumePercent(int percent);
 bool audioBtIsConnected();

 Krok 7 — nfc_module

 nfcInit() zachowuje critical section z setup():
 bool nfcInit() {
     if (nfcCriticalBegin(portMAX_DELAY)) {
         nfcReady = nfcInitSequence();
         nfcCriticalEnd();
     }
     return nfcReady;
 }
 nfcCriticalBegin/End — prywatne w nfc_module.cpp.

 Publiczny interfejs:
 bool nfcInit();
 void nfcStartTask();
 bool nfcGetEvent(NfcEvent *e, TickType_t timeout);
 void nfcStopTaskForSleep();
 void nfcPowerDown();

 RTC_DATA_ATTR bool rtcNfcPowerDownSent — jeśli static RTC_DATA_ATTR nie działa z linkerem ESP-IDF, przenieść do state.cpp.

 Krok 8 — buttons_isr

 buttons[] jako extern — dostępne dla buttons.cpp:
 // buttons_isr.h
 #pragma once
 #include "shared_types.h"
 #include "musicbox_config.h"   // BTN_COUNT — nie redefiniować tutaj

 extern Button buttons[BTN_COUNT];
 void buttonsInit();

 Weryfikacja IRAM po kroku 8:
 xtensa-esp32-elf-size .pio/build/lolin_d32_pro/firmware.elf
 grep btnISR .pio/build/lolin_d32_pro/firmware.map
 # Akceptacja: "btnISR" w sekcji ".iram0.text", nie ".flash.text"

 ---
 Zależności między modułami (graf include)

 musicbox_config.h  <- wszystko
 logging.h          <- wszystkie .cpp (nie persistent_log — ma własny PLOGF)
 shared_types.h     <- buttons_isr, nfc_module, playback, main
 helpers.h          <- sd_storage, nfc_module, sync_mode, buttons, testy natywne
 state.h            <- leds, volume, sd_storage, audio, nfc, playback, sleep, buttons, sync, main
 leds               <- musicbox_config.h, logging.h, state.h
 jbl                <- musicbox_config.h, logging.h
 battery            <- musicbox_config.h, logging.h
 volume             <- musicbox_config.h, logging.h, state.h, leds.h, audio.h
 sd_storage         <- musicbox_config.h, logging.h, state.h (figurineMap)
 audio              <- musicbox_config.h, logging.h, state.h (isPlaying, g_btConnected), leds.h
 nfc_module         <- musicbox_config.h, logging.h, state.h (nfcReady), leds.h, helpers.h, shared_types.h
 buttons_isr        <- musicbox_config.h, shared_types.h
 playback           <- musicbox_config.h, logging.h, state.h, jbl.h, leds.h, audio.h
 sleep              <- musicbox_config.h, logging.h, state.h, audio.h, nfc_module.h, jbl.h, leds.h, playback.h
 buttons            <- musicbox_config.h, state.h, buttons_isr.h, helpers.h, volume.h, leds.h, sleep.h, battery.h, playback.h
 sync_mode          <- musicbox_config.h, logging.h, state.h, leds.h, sd_storage.h, helpers.h
 main.cpp           <- wszystkie moduły

 ---
 Pliki krytyczne

 - esp32/src/main.cpp — źródło do podziału
 - esp32/platformio.ini — src_filter = -<*> w env:native: nowe .cpp w src/ do build lolin_d32_pro, NIE do testów natywnych
 - esp32/test/test_uid_format/test_uid_format.cpp — wymaga aktualizacji (Krok 0b)
 - esp32/test/test_volume/test_volume.cpp — wymaga aktualizacji (Krok 0b)
 - esp32/test/Arduino.h — stub; sprawdzić obsługę String dla helpers.h
 - esp32/src/persistent_log.h — wzorzec dla nowych modułów

 ---
 Weryfikacja po każdym kroku

 Każdy krok:
 1. pio run — brak błędów kompilacji
 2. Rozmiary sekcji:
 xtensa-esp32-elf-size .pio/build/lolin_d32_pro/firmware.elf
 2. Porównać .text, .data, .bss — refaktor nie powinien ich zmieniać znacząco.
 3. Flash + boot log — sprawdzenie sekwencji init

 Baseline do zmierzenia PRZED startem (HW):
 [AUDIO] Stack HWM: ??? B
 [NFC]   Stack HWM: ??? B
 [LED]   Stack HWM: ??? B
 ESP.getMinFreeHeap(): ??? B
 Boot-to-play: ??? ms
 .text: ??? B  .data: ??? B  .bss: ??? B

 Po finalnym kroku 13:
 1. pio test -e native — 31 testów zielonych
 2. pytest integration/test_boot_sequence.py — timing OK
 3. pytest integration/test_heartbeat.py — 60s soak (heap, HWM)
 4. Pełna sesja 30+ min: boot → NFC → muzyka → VOL+/- → sleep → wake → sync trigger
 5. Porównanie wszystkich wartości baseline — brak regresji