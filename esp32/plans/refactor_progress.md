# Postęp refaktoru main.cpp → moduły

Branch: `bluetooth`
Plan: `esp32/plans/refactor.md`

## Status kroków

| Krok | Moduł | Status | Commit |
|------|-------|--------|--------|
| 0a | musicbox_config.h | ✅ DONE | 35a35d7 |
| 0b | logging.h + helpers.h | ✅ DONE | 35a35d7 |
| 0c | shared_types.h + state.h/cpp | ✅ DONE | 60f4ee7 |
| 1 | battery.h/.cpp | ✅ DONE | 4b61aff |
| 2 | leds.h/.cpp | ✅ DONE | 1a74292 |
| 3 | jbl.h/.cpp | ✅ DONE | e598a41 |
| 4 | volume.h/.cpp | ✅ DONE | a51a00f |
| 5 | sd_storage.h/.cpp | ✅ DONE | 0c029c3 |
| 6 | audio.h/.cpp | ✅ DONE | 58a0c20 |
| 7 | nfc_module.h/.cpp | ✅ DONE | ff698bb |
| 8 | buttons_isr.h/.cpp | ✅ DONE | 4d580b6 |
| 9 | playback.h/.cpp | ✅ DONE | 19ba186 |
| 10 | sleep.h/.cpp | ✅ DONE | 967fba5 |
| 11 | buttons.h/.cpp | ✅ DONE | ff0bdaf |
| 12 | sync_mode.h/.cpp | ✅ DONE | ac7d42d |
| 13 | cleanup main.cpp | ✅ DONE | 2731b10 |

## Pliki już wyekstrahowane

```
esp32/src/
├── musicbox_config.h   ✅ (#define, piny, stałe, BTN_COUNT, ENABLE_LEDS)
├── logging.h           ✅ (LOG/LOGLN)
├── helpers.h           ✅ (inline: urlEncode, uidToString, batteryBars)
├── shared_types.h      ✅ (Button struct, NfcEvent struct)
├── state.h/.cpp        ✅ (shared globals z extern)
├── battery.h/.cpp      ✅ (readBatteryVoltage)
├── leds.h/.cpp         ✅ (LED task, animacje, pełne API)
├── jbl.h/.cpp          ✅ (isJblOn, jblPowerOff, jblPressButtonBlocking)
├── persistent_log.h/.cpp ✅ (niezmieniony — ma własny PLOGF)
├── volume.h/.cpp       ✅
├── sd_storage.h/.cpp   ✅
├── audio.h/.cpp        ✅
├── nfc_module.h/.cpp   ✅
├── buttons_isr.h/.cpp  ✅ (buttons[], btnISR IRAM, buttonsInit)
├── playback.h/.cpp     ✅ (ensureJblReady, startPlayback, stopPlayback, playSystemSoundSync)
├── sleep.h/.cpp        ✅ (enterDeepSleep, handleWakeFromDeepSleep)
├── buttons.h/.cpp      ✅ (handleButtons — logika akcji)
├── sync_mode.h/.cpp    ✅ (runSyncMode, wszystkie WiFi helpers — static)
└── main.cpp            ✅ (~361 linii: tylko setup() + loop())
```

## Stan main.cpp po kroku 3

Pozostałe sekcje do ekstrakcji w main.cpp:
- `btVolume`, `saveBtVolume`, `loadBtVolume`, `applyBtVolume`, `volumeUp`, `volumeDown` → **krok 4: volume.h/.cpp**
- `initSD`, `readJsonFromSd`, `loadMappings`, `loadSystemSounds` → **krok 5: sd_storage.h/.cpp**
- `AudioInfoLogger`, `a2dp`, `mp3Decoder`, `decoderStream`, `audioTaskFunc`, `AudioCmd/Type`, `audioQueue`, `audioTaskHandle` → **krok 6: audio.h/.cpp**
- `nfc`, `nfcMutex`, `nfcQueue`, `nfcTaskHandle`, NFC init/task/PowerDown → **krok 7: nfc_module.h/.cpp**
- `buttons[]`, `btnISR`, `buttonsInit` → **krok 8: buttons_isr.h/.cpp**
- `ensureJblReady`, `startPlayback`, `stopPlayback`, `playSystemSoundSync` → **krok 9: playback.h/.cpp**
- `enterDeepSleep`, `handleWakeFromDeepSleep` → **krok 10: sleep.h/.cpp**
- `handleButtons` → **krok 11: buttons.h/.cpp**
- `syncServerIP`, `runSyncMode`, `performSync`, `syncDownloadFile`, itp. → **krok 12: sync_mode.h/.cpp**
- Cleanup setup()+loop() → **krok 13**

## Zależności między modułami (z refactor.md)

```
musicbox_config.h  ← wszystko
logging.h          ← wszystkie .cpp (nie persistent_log)
shared_types.h     ← buttons_isr, nfc_module, playback, main
helpers.h          ← sd_storage, nfc_module, sync_mode, buttons, testy
state.h            ← leds, volume, sd_storage, audio, nfc, playback, sleep, buttons, sync, main
leds.h             ← musicbox_config, logging, state
jbl.h              ← musicbox_config, logging
battery.h          ← musicbox_config, persistent_log
volume.h           ← musicbox_config, logging, state, leds, audio
sd_storage.h       ← musicbox_config, logging, state (figurineMap)
audio.h            ← musicbox_config, logging, state, leds
nfc_module.h       ← musicbox_config, logging, state, leds, helpers, shared_types
buttons_isr.h      ← musicbox_config, shared_types
playback.h         ← musicbox_config, logging, state, jbl, leds, audio
sleep.h            ← musicbox_config, logging, state, audio, nfc_module, jbl, leds, playback
buttons.h          ← musicbox_config, state, buttons_isr, helpers, volume, leds, sleep, battery, playback
sync_mode.h        ← musicbox_config, logging, state, leds, sd_storage, helpers
main.cpp           ← wszystkie moduły
```

## Kluczowe uwagi dla krok 4 (volume)

- `applyBtVolume()` woła `a2dp.setVolume()` → volume.cpp musi znać a2dp lub dostać API z audio
- Zgodnie z planem: `audioSetBtVolumePercent(int percent)` to publiczne API audio.h
- `volumeUp/Down()` w volume.cpp wołają `audioSetBtVolumePercent()` (nie `a2dp` bezpośrednio)
- `btVolume` = prywatny w volume.cpp (static)
- `Preferences preferences` = prywatny w volume.cpp (static)
- Ale `applyBtVolume()` jest też wołana z loop() w main.cpp przy BT connect — API: `volumeApplyToBt()`

## Kluczowe uwagi dla krok 6 (audio)

- `a2dp`, `mp3Decoder`, `decoderStream`, `audioQueue`, `audioTaskHandle` → static w audio.cpp
- `AudioCmdType`, `AudioCmd` → private w audio.cpp
- `onBtStateChange` → static w audio.cpp, rejestrowany wewnątrz `audioInit()`
- `g_btConnected` = extern ze state.h — ustawiany przez `onBtStateChange` w audio.cpp
- Publiczny interfejs audio.h:
  ```cpp
  void audioInit();
  void audioStartFile(const char *path);
  void audioStop();
  bool audioIsRunning();
  void audioDeleteTaskForSleep();
  void audioSetBtVolumePercent(int percent);
  bool audioBtIsConnected();
  uint32_t audioGetTaskHWM();
  ```
- `decoderStream.begin()` i `mp3Decoder.addNotifyAudioChange()` → do `audioInit()`
- BT A2DP init z setup() → do `audioInit()`

## Kluczowe uwagi dla krok 7 (nfc_module)

- `nfc`, `nfcMutex`, `nfcQueue`, `nfcTaskHandle`, `nfcTaskStopRequested` → static w nfc_module.cpp
- `RTC_DATA_ATTR bool rtcNfcPowerDownSent` → static RTC w nfc_module.cpp
- `nfcErrorCount` → static w nfc_module.cpp (przeniesiony z main.cpp)
- `nfcCriticalBegin/End` → private w nfc_module.cpp
- Publiczny interfejs nfc_module.h:
  ```cpp
  bool nfcInit();
  void nfcStartTask();
  bool nfcGetEvent(NfcEvent *e, TickType_t timeout);
  void nfcStopTaskForSleep();
  void nfcPowerDown();
  uint32_t nfcGetTaskHWM();
  ```

## Weryfikacja po każdym kroku

1. `pio run -e lolin_d32_pro` → SUCCESS
2. `pio test -e native` → 31/31 PASSED
3. Flash na HW + sprawdzenie boot logu (przed krytycznymi krokami)
