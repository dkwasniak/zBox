# Review firmware ESP32 — MusicBox

Data: 2026-06-12 · Branch: `modul_ns` · Zakres: wyłącznie `esp32/src/` (+ `sdkconfig`, `platformio.ini`, submoduł `lib/ESP32-A2DP` w zakresie `end()`)

Objawy zgłoszone: (1) sporadyczne, krótkie zacięcia audio podczas odtwarzania; (2) zaskakująco szybkie rozładowanie baterii 4000 mAh „podczas snu".

**Kontekst architektury (zastany stan):** reducer jest czysty ✓, ISR-y tylko postują do kolejek ✓, single-owner per domena ✓. Wszystkie taski aplikacyjne (dispatcher „app" prio 1, audio prio 2, LED prio 1, btnadapt prio 2, nfc prio 1, loopTask) są przypięte do **core 1**; core 0 ma tylko stack BT + worker `btctrl`. NFC jest na bit-bangowanym SoftSPI (GPIO 22/21/0/5), SD na sprzętowym SPI (18/19/23) — **brak konfliktu magistrali SD↔NFC**. CPU 160 MHz (nie 240) ✓.

---

## 1. Zacięcia audio

### A1. Zapisy logu na SD z taska `loop()` konkurują z odczytem audio z SD — **wysokie / mechanizm potwierdzony w kodzie, związek z objawem: silna hipoteza**

- `src/main.cpp:276` — `plogFlushToSd()` w heartbeacie loop(); `src/util/persistent_log.cpp:166-181` — każdy flush robi `plogRotateIfNeeded()` (otwarcie `/data/debug.log` do odczytu rozmiaru), potem `SD.open(FILE_APPEND)` + pętla `println` + `close`.
- **Mechanizm:** task audio czyta plik z SD w pętli (`src/audio/audio.cpp:612`). Append do FAT (aktualizacja łańcucha FAT + wpisu katalogu) na typowej karcie trwa 50–500 ms i serializuje się z odczytem audio na poziomie FatFs/SPI. Bufor DMA I2S to ledwie kilkadziesiąt ms audio — dłuższy stall = słyszalna przerwa, po której odtwarzanie wraca. Flush odpala się najwyżej co 10 s i **tylko gdy są nowe linie WARN/ERROR/CRIT** (LOGW są persistent — `logging.h:16`), a te generują się sporadycznie (np. `[LED] frame gap`, `[AUDIO] slow decode`) — stąd nieregularność objawu („raz na jakiś czas").
- **Poprawka:** w `plogFlushToSd()` wyjść natychmiast, gdy `audioIsRunning()` (flush dogoni przy idle/przed snem); `plogRotateIfNeeded()` wykonywać co N-ty flush. Ryzyko regresji: niskie — linie i tak siedzą w ringu RTC (40×96 B, odzyskiwane po crashu); jedyny koszt to opóźnienie logu na SD. Weryfikacja: `pio run -e lolin_d32_pro`, `pio test -e native` (bez wpływu); **wymaga testu na sprzęcie** — odtwarzanie 30–60 min z wymuszanymi LOGW i nasłuch zacięć.

### A2. Blokujące logowanie w hot-path taska audio + sprzężenie przez `plogMutex` — **wysokie / potwierdzone w kodzie**

- `src/util/persistent_log.cpp:141-164` — każde LOGx to 2×`snprintf` + **blokujący** `Serial.println` (115200 bd ≈ 9 ms na 100-znakową linię). W tasku audio: heartbeat (`audio.cpp:517-521`), `[AUDIO_BT_START]` (617-625), `slow decode` LOGW (626-633). Gorzej: LOGW/E/C biorą `plogMutex` (timeout 50 ms), a `plogFlushToSd()` **trzyma ten mutex przez cały zapis na SD** (`persistent_log.cpp:175-179`) — task audio logujący ostrzeżenie potrafi czekać do 50 ms dokładnie wtedy, gdy trwa flush z A1 (efekty się sumują).
- **Poprawka:** (a) pod mutexem tylko kopiować pending-linie do lokalnego bufora, zapis SD poza mutexem; (b) logi z taska audio rate-limitować albo zdegradować do nietrwałych. Ryzyko: niskie (czysta zmiana sekcji krytycznej, semantyka logów bez zmian). Weryfikacja: `pio run`; soak-test na sprzęcie zalecany.

### A3. Churn heapu: `String` w pętli NFC co 1 s, `end()/begin()` dekodera per utwór — **średnie / potwierdzone w kodzie (skutek: hipoteza)**

- `src/nfc/nfc_module.cpp:249-288` + `util/helpers.h:28-41` — każdy poll (co `NFC_READ_INTERVAL`) alokuje kilka obiektów `String`; `src/audio/audio.cpp:383-397` — pełny `decoderStream.end()/begin()` przy każdym utworze (realloc buforów Helix); do tego `std::map<String,String>` (`sd_storage.cpp:11-12`) i `std::vector<String>` (`playback.cpp:18`).
- **Mechanizm:** wielogodzinna sesja → fragmentacja → malejący largest-free-block (już logowany: `audio.cpp:665`) → wolniejsze/nieudane alokacje w torze audio.
- **Poprawka (bezpieczna część):** w pętli NFC zastąpić `String` buforem `char[32]` (formatowanie UID bez heapu) — **zero zmian w cadence odczytów**. Mapy/wektory zostawić (alokują przy boot/sync). Restrukturyzacja dekodera **odrzucona** — ryzyko regresji toru audio przewyższa zysk, dopóki telemetria largest-block nie potwierdzi problemu. Weryfikacja: `pio test -e native` (test_uid_format wymaga aktualizacji sygnatur), `pio run`; obserwacja `[AUDIO] Track ended heap=... largest=...` w terenie.

### A4. `BtPcmBufferSink`: wielokilobajtowe `memcpy` do/z PSRAM w `portENTER_CRITICAL` — **średnie (dotyczy trybu BT) / potwierdzone w kodzie**

- `src/audio/audio.cpp:276-311` — `writeSome`/`readSome` kopiują dane wewnątrz spinlocka (przerwania wyłączone na rdzeniu wołającym); bufor 15 360 B leży w PSRAM (linie 204-205). `readForA2dp` woła task stosu BT na **core 0** — kopie z PSRAM (wolniejsze, z `-mfix-esp32-psram-cache-issue`) przy wyłączonych przerwaniach core 0 zaburzają timing kontrolera BT.
- **Poprawka:** trzymać w sekcji krytycznej wyłącznie aktualizacje indeksów (rezerwacja zakresu → kopiowanie poza lockiem → commit), ewentualnie przenieść bufor do RAM wewnętrznego (15 KB się zmieści). Ryzyko: średnie (łatwo o race przy złej implementacji dwufazowej) — wymaga testu na sprzęcie w trybie BT; logika audio poza tym nietknięta. Weryfikacja: `pio run` + odsłuch BT.

### A5. `CONFIG_BTDM_CTRL_MODEM_SLEEP=y` (ORIG) przy źródle A2DP — **niskie–średnie / hipoteza**

- `sdkconfig.defaults` — modem sleep kontrolera BT to znana przyczyna okresowych zacięć A2DP source na ESP32. Dotyczy wyłącznie trybu słuchawek. Poprawka: wyłączyć modem sleep — ale to podnosi pobór w trybie BT; rekomendacja: tylko **eksperyment na sprzęcie** (jeśli zacięcia występują też na słuchawkach), nie zmiana na stałe.

### A6. Dispatcher loguje LOGI przy każdym evencie/efekcie — **niskie / potwierdzone**

- `dispatcher.cpp:345, 296` — blokujący Serial na tasku dispatchera; nie zacina audio bezpośrednio, ale dodaje latencję do wykonania efektów (w tym komend startu audio).

### Wykluczone (warto odnotować)

- **FastLED RMT vs BT na core 0** (hipoteza z `docs/random_hang.md`): task LED jest tworzony na **core 1** (`leds.cpp:317`), więc RMT-ISR rejestruje się na core 1; show() dla 12 diod ≈ 360 µs przy prio 1 < audio prio 2. Mało prawdopodobny sprawca zacięć audio.
- **NFC**: brak wspólnej magistrali z SD, prio 1 < audio 2 — odczyty NFC nie wywłaszczają audio. `nfcCriticalBegin` zawiesza task LED na czas odczytu (20–90 ms co sekundę) — to może powodować mikro-zacięcia **animacji LED**, nie audio (obserwacja, bez propozycji zmian timingu).

### Top 3 najbardziej prawdopodobne przyczyny zacięć

1. **A1** — okresowy append logu do SD z `loop()` blokujący odczyt audio z tej samej karty (periodyczność i losowość idealnie pasują do objawu);
2. **A2** — blokujące `Serial.println` + czekanie na `plogMutex` w samym tasku audio, kumulujące się z A1;
3. **A3/A4** — fragmentacja heapu w długich sesjach (głośnik lokalny) oraz kopie PSRAM w sekcjach krytycznych (tryb BT).

---

## 2. Energia

### E1. Wyjście z trybu BT **nie wyłącza kontrolera BT** — `btA2dp.end(false)` — **krytyczne (złamana twarda reguła radia) / potwierdzone w kodzie**

- `src/audio/audio.cpp:807` → `lib/ESP32-A2DP/src/BluetoothA2DPCommon.cpp:131-235`: przy `release_memory=false` wykonywany jest tylko deinit profili A2DP/AVRC; `esp_bluedroid_disable/deinit` i `esp_bt_controller_disable/deinit` są **wyłącznie w gałęzi `release_memory=true`**. Po wyjściu z trybu słuchawek urządzenie gra dalej z głośnika z **włączonym radiem BR/EDR** (modem-sleep łagodzi, ale to nadal mA-poziom ciągłego poboru) aż do deep sleep.
- **Poprawka:** po `end(false)` dołożyć ręczny teardown: `esp_bluedroid_disable()`+`deinit()`, `esp_bt_controller_disable()`+`deinit()` — **bez** `esp_bt_controller_mem_release` (release uniemożliwiłby ponowny start trybu BT do rebootu; biblioteka po `Source::end` zeruje `is_bluedroid_initialized` — `BluetoothA2DPSource.cpp:190` — więc `start_raw` ponownie zainicjalizuje bluedroid, ale kontroler trzeba będzie re-init analogicznie do `initBtForScan()` w sync_mode). **Ryzyko regresji: wysokie** — to dokładnie okolica znanego crasha przy rozłączaniu słuchawek (submoduł ma już lokalne łatki, 29 insertions). Wdrażać osobnym commitem, z pełnym cyklem na sprzęcie: start BT → graj → stop → start ponowny → rozłączenie słuchawek z ich strony → sleep. `pio run` + `pio test -e native` (test_bt_adapter) przed i po. **Wymaga testu na sprzęcie.**

### E2. Sync mode bez timeoutu + flaga `sync_pending` przeżywa crash — **krytyczne dla scenariusza drenażu / potwierdzone w kodzie**

- `src/modes/sync_mode.cpp:1374-1417` — `for(;;)` bez żadnego limitu bezczynności; flaga kasowana tylko w `exitSyncMode()` (598) i przy timeoucie portalu (1359). `src/main.cpp:185-191` — każdy boot z istniejącą flagą wchodzi w sync.
- **Mechanizm:** przypadkowe A+B (urządzenie dla dziecka!) → restart do sync → WiFi + 160 MHz + miganie LED **bez końca** ≈ 80–120 mA → 4000 mAh znika w 1,5–2 doby. Crash w sync → reboot → znowu sync (flaga na SD). Jeśli użytkownik sądzi, że urządzenie „śpi", to jest najprostsze wyjaśnienie szybkiego drenażu.
- **Poprawka:** (a) timeout bezczynności w pętli sync (np. 30 min od ostatniego żądania HTTP/OTA → `exitSyncMode("idle timeout")`); (b) licznik wejść w sync z rzędu (NVS) — po 2 nieudanych skasować flagę. Ryzyko: niskie (sync to tryb serwisowy). Weryfikacja: `pio run`; ręcznie: wejść w sync, nie dotykać, sprawdzić restart po timeoucie.

### E3. Sekwencja wejścia w deep sleep nie odcina peryferiów — **wysokie / mechanizm potwierdzony, skala poboru do zmierzenia**

- `src/power/sleep.cpp:29-46` (`sleepExecuteDeepSleep`): przed `esp_deep_sleep_start()` jest tylko: delete audio taska, NFC PowerDown, LED off (MOSFET przez `LED_EN`→INPUT — to akurat zrobione dobrze, `leds.cpp:455-460`). Brakuje:
  - **SD**: brak `SD.end()`/`SPI.end()`; CS (GPIO4) i linie SPI floatują w deep sleep → karta może nie wejść w standby (typowo 100 µA–2 mA, wadliwe karty więcej);
  - **Wzmacniacz NS4168**: firmware **w ogóle nie steruje pinem enable** (brak w `zbox_config.h`); piny I2S 32/33/13 po zaśnięciu floatują → wejścia wzmacniacza nieokreślone (możliwy pobór/szum). Czy NS4168 ma wyprowadzony CTRL na PCB — do weryfikacji poza firmware;
  - **GPIO hold/izolacja**: brak `gpio_hold_en`/`rtc_gpio_isolate` dla CS, linii PN532 (SS/SCK/MOSI floatują w stronę zasilanego PN532), I2S.
- **Poprawka:** przed snem: `SD.end()` + `SPI.end()`, CS na OUTPUT HIGH + `gpio_hold_en`, piny I2S OUTPUT LOW + hold, `PN532_SS` HIGH + hold; po wake (początek `setup()`) `gpio_hold_dis` na tych pinach. Ryzyko: **średnie** — zapomniany `hold_dis` po wake da „SD FAIL"/martwe NFC po przebudzeniu; zmiana dotyka sekwencji snu i budzenia. Weryfikacja: `pio run`; **wymaga testu na sprzęcie z pomiarem µA** (przed/po, każda ścieżka: sleep z przycisku, idle, night-light, emergency).

### E4. PN532 może przespać noc **obudzony** — ścieżki błędów PowerDown — **wysokie (skutek katastrofalny, ścieżka rzadka) / potwierdzone w kodzie**

- `src/nfc/nfc_module.cpp:409-433`: `nfcPowerDown()` rezygnuje przy zajętym busie (timeout 1 s) lub braku acku — urządzenie i tak zasypia, a PN532 zostaje w trybie normalnym (komentarz w kodzie: ~100 mA vs ~1 mA) → bateria pada w 1–2 doby. Dodatkowo drugi wywołany `nfcPowerDown` (po wcześniejszym `stopNfcForMusicMode`) wchodzi w gałąź „not ready" i **nadpisuje `rtcNfcPowerDownSent=false`** (linia 414) mimo wysłanego wcześniej PowerDown — psuje raw-wake przy następnym boocie (wolniejszy init przez retry).
- **Poprawka:** retry PowerDown (3× z krótkim odstępem) i wpis CRIT do plog przy ostatecznej porażce; w gałęziach „skipped" nie zerować flagi RTC, jeśli PowerDown był już wysłany. **Cadence odczytów nietknięte.** Ryzyko: niskie. Weryfikacja: `pio run`; sprzęt: pomiar prądu po śnie + log `[SLEEP] NFC PowerDown ... ack=1`.

### E5. Sleep z sync mode bez `esp_wifi_stop()` i bez teardownu BT — **średnie / potwierdzone w kodzie**

- `sync_mode.cpp:1384-1389` → `sleep.cpp:13-27` (`enterDeepSleep`): WiFi nadal połączone, po skanie BT kontroler może być aktywny; IDF dokumentuje podwyższony pobór deep sleep bez uprzedniego `esp_wifi_stop()`. Poprawka: w ścieżce BTN_D w sync wywołać `cleanupBtForSyncExit()` + `WiFi.disconnect(true)` + `esp_wifi_stop()` przed `enterDeepSleep()`. Ryzyko: niskie. **Pomiar na sprzęcie.**

### E6. Możliwość „utknięcia na zawsze obudzonym": `PlayingFile` bez idle-deadline + droppable `TrackEnded` — **średnie / hipoteza poparta kodem**

- `reducer.cpp:373-374, 386-387` — podczas grania `idle_deadline_ms=0` (słusznie); powrót deadline'u zależy od `TrackEnded`/`AudioStopped`. `TrackEnded` ma domyślną politykę **Coalescible** (`event_queue.cpp:30`) i przy pełnej kolejce (głębokość **16**, `event_queue.h:6`) jest po cichu dropowany — a dispatcher bywa blokowany ~3,7 s (patrz I1). Wynik: stan `PlayingFile` na zawsze, urządzenie nigdy nie zaśnie → noc po cichu zjada baterię.
- **Poprawka:** watchdog spójności w dispatcherze (co ~30 s: `audio_state==PlayingFile && !audioIsRunning()` → syntetyczny `AudioStopped`) lub polityka retry dla `TrackEnded`. Testowalne natywnie (`pio test -e native` — nowy test reducera/dispatchera). Ryzyko: niskie.

### E7. Drobiazgi energetyczne — **niskie / potwierdzone**

- Dzielnik baterii on-board 2×100k stale pod VBAT ≈ 18–20 µA (hardware, nie do naprawy w FW; ~0,5% pojemności/miesiąc — pomijalne).
- `CONFIG_PM_ENABLE` wyłączone, brak DFS: pobór aktywny stały (160 MHz — dobrze, że nie 240); akceptowalne, bo idle→deep sleep po 10 min.
- Tick taska LED co 15 ms nawet w `LED_OFF` (`leds.cpp:920-932`) — niepotrzebne budzenie CPU; w LED_OFF można spać 200 ms+ (uwaga na responsywność przejść scen).
- Pull-up BTN_D podczas deep sleep: `INPUT_PULLUP` na padzie RTC GPIO26 powinien przetrwać (pull w domenie RTC, ext0 trzyma RTC_PERIPH zasilone), ale brak jawnego `rtc_gpio_pullup_en` — jeśli pin jednak floatuje, sporadyczne wybudzenia drenują baterię; zweryfikować licznikiem wpisów BOOT/`wake_cause` w plog po nocy. Hipoteza.
- `handleWakeFromDeepSleep`: abort <400 ms → szybki powrót do snu ✓ dobrze zrobione.

### Weryfikacja twardej reguły radia — ścieżka po ścieżce

| Ścieżka | Status |
|---|---|
| Boot normalny / music / card / night-light | ✓ radia nieinicjalizowane (grep: WiFi tylko w `sync_mode.cpp`; `audioInit` nie dotyka kontrolera) |
| BT mode start (długi BTN_A) | ✓ jedyna ścieżka startu BT poza sync |
| **BT mode exit** | ✗ **E1 — kontroler i bluedroid zostają ENABLED** |
| **BT start failure** | ✗ `audio.cpp:448-453` — po nieudanym `start_raw` brak teardownu; stack może zostać częściowo zainicjalizowany na stałe (naprawić razem z E1) |
| Rozłączenie słuchawek | ✓ wg reguły (tryb BT trwa, okno reconnect do idle-timeoutu) |
| Sync mode: WiFi | ✓ tylko tam; ✓ BT startuje wyłącznie po żądaniu HTTP `/bt/start-scan` (nigdy automatycznie) |
| Sync exit (A+B / HTTP restart / portal timeout) | ✓ `cleanupBtForSyncExit()` + restart |
| **Sleep z sync (BTN_D)** | ✗ E5 — bez wifi_stop/BT cleanup |
| Wejście w deep sleep (normalne) | ✓ fizycznie radio gaśnie; bez czystego deinit (akceptowalne) |
| Martwy kod | `releaseBtMemoryIfUnused()` (`sync_mode.cpp:117-124`) — nigdy niewywoływane; do usunięcia przy okazji |

### Top podejrzani drenażu (w kolejności prawdopodobieństwa)

1. **Urządzenie wcale nie śpi** — sync mode bez timeoutu / flaga po crashu (E2) albo utknięcie w `PlayingFile` (E6): jedyne scenariusze tłumaczące rozładowanie 4000 mAh w dni, a nie miesiące;
2. **PN532 nieuśpiony po nieudanym PowerDown** (E4) — dziesiątki mA przez całą noc, ścieżka rzadka, skutek identyczny z objawem;
3. **Karta SD + wzmacniacz z floatującymi liniami w deep sleep** (E3) — setki µA–pojedyncze mA, czyli tygodnie zamiast miesięcy;
4. Radio BT pozostawione po sesji słuchawkowej (E1) — drenaż aktywny, do najbliższego snu;
5. (obserwacja, bez propozycji zmian) cadence NFC i pull-up BTN_D w sleep: pull pada RTC powinien przetrwać deep sleep, ale warto policzyć wybudzenia — `wake_cause` jest już logowany do plog przy każdym boocie; jeśli nocą przybywa wpisów BOOT, to spurious wake'i.

---

## 3. Inne (wydajność i stabilność)

### I1. `ledShowBattery` blokuje dispatcher na ~2,3–3,7 s — **wysokie / potwierdzone w kodzie**

- `dispatcher.cpp:181` → `leds.cpp:680-729`: scena BatteryPreview wykonuje na **tasku dispatchera** sweep (480 ms) + hold (1500 ms) + opcjonalne miganie (1380 ms) + sweep-out (360 ms), wszystko przez `delay()`. Przez ten czas: zero obsługi eventów (kolejka tylko 16), przyciski martwe, deadline'y stoją, a przepełnienie kolejki eventem Critical = `esp_restart` (patrz I2).
- **Poprawka:** przenieść animację do taska LED (nowy tryb `LED_BATTERY` sterowany sceną, jak pozostałe animacje) — nieblokująco. Ryzyko: średnie (nowa maszyna stanów animacji); weryfikacja `pio run` + sprzęt (BTN_C-long podczas grania, klikać inne przyciski). `ledShutdownAnim` w ścieżce snu też blokuje 2,4 s — tam akceptowalne, nie ruszać.

### I2. Pełna kolejka + event Critical → natychmiastowy `esp_restart` — **średnie / potwierdzone**

- `event_queue.cpp:39-41`: `SleepRequested`/`BtHeadphonesModeStopped` przy pełnej kolejce restartują urządzenie bez próby ponowienia. W połączeniu z I1 scenariusz realny. **Poprawka:** przed restartem retry `xQueueSend` z timeoutem ~100 ms. Ryzyko: minimalne. Test natywny możliwy (`test_dispatcher`).
- `postEventFromIsr`: drop Critical bez śladu (komentarz w kodzie to przyznaje) — przyciski nie emitują Critical z ISR; akceptowalne, odnotowane.

### I3. `audioDeleteTaskForSleep` — `vTaskDelete` taska w nieznanym stanie — **średnie / potwierdzone (wzorzec „działa przypadkiem")**

- `audio.cpp:770-775` ← `sleep.cpp:35`: kill taska, który może właśnie trzymać lock FatFs, heap-lock (malloc w dekoderze) albo być w `decoderStream.write`. Dziś ścieżka snu po tym wywołaniu prawie nie używa SD/heapu, więc uchodzi — ale każda przyszła zmiana sekwencji snu (np. flush logu po delete) może się zawiesić. **Poprawka:** STOP przez istniejący mechanizm cmd_id (`AudioStopped` już istnieje) + `vTaskSuspend` zamiast delete. Ryzyko: wydłuża wejście w sen o ułamki sekundy; **wymaga testu na sprzęcie** (wszystkie ścieżki snu).

### I4. Stacki i watchdog — stan dobry, do potwierdzenia danymi z pola — **niskie**

- HWM logowane co 30 s dla app/led/audio/nfc (`dispatcher.cpp:381-388`, `main.cpp:281-285`) ✓; WDT 15 s panic na loop+dispatcher ✓. Stacki: app 8192, audio 8192, pozostałe 4096; `i2s_init` 2048 (jednorazowy, ciasny). Tasków audio/nfc/led nie ma w WDT — zawieszenie audio wykryje tylko brak heartbeatu w logu. Rekomendacja: bez zmian rozmiarów, dopóki HWM z terenu nie pokaże <~512 B („measure before").
- `buttonAdapterISR`: `millis()`+`digitalRead` w ISR — obie funkcje są IRAM w arduino-esp32 ✓ OK.

### I5. Obsługa błędów SD w runtime — **niskie / potwierdzone**

- `audio.cpp:656` — wyjęcie karty podczas grania wygląda jak koniec utworu (brak rozróżnienia błędu odczytu od EOF); `sdReady` ustawiane raz na boot (`main.cpp:168`), brak detekcji utraty karty. Kolejne starty kończą się czysto obsłużonym `FileNotFound` ✓. Akceptowalne; ewentualnie dźwięk błędu przy serii niepowodzeń.
- `playback.cpp:111` — lazy `refreshMusicLibrary()` na tasku dispatchera przy starcie utworu (skan katalogu + `String`): przy dużej bibliotece setki ms blokady. Poprawka: refresh tylko na boot (już jest w `playbackInit`) — usunąć lazy-fallback albo zwracać błąd. Ryzyko: niskie; `pio test -e native` (test_mapping).
- `initSD`: `SD.begin(SD_CS)` z domyślną częstotliwością 4 MHz — zapas przepustowości dla MP3 jest (≈10×), nie ruszać bez pomiarów.

### I6. Drobiazgi — **niskie**

- `nightLightTick()` (`night_light.cpp:89`) — martwy kod (timeout przejął reducer ✓ spójnie); `jbl.cpp` — pusty stub; `releaseBtMemoryIfUnused` — martwy kod (patrz tabela radiowa).
- Drift dokumentacji vs kod: skill mówi „queue 32 slots", realnie **16** (`event_queue.h:6`); wspomniany `BtState::Disabled` nie istnieje w `app_state.h`.
- `s_isPlaying`/`s_isPaused`/`btTransportStarted` (`audio.cpp:348-352`) czytane między taskami bez `volatile`/atomic — w praktyce bezpieczne (wywołania między jednostkami translacji), dla porządku `std::atomic<bool>`.

---

## Podsumowanie weryfikacyjne

- Wszystkie poprawki są weryfikowalne przez `pio run -e lolin_d32_pro` + `pio test -e native`; dodatkowego **testu na sprzęcie** wymagają: A1, A2 (soak), A4, A5 (eksperyment), E1 (pełny cykl BT), E3/E4/E5 (pomiar prądu snu), I1, I3.
- Poprawki **odrzucone** (ryzyko > zysk): restrukturyzacja `end()/begin()` dekodera per utwór (A3), zmiana częstotliwości SPI SD, zmiany priorytetów/pinningu tasków (obecny układ jest spójny z koegzystencją BT na core 0), jakiekolwiek zmiany cadence/timingu NFC (wszystkie uwagi NFC to obserwacje lub zmiany niedotykające cadence).
- Sugerowana kolejność wdrażania: **E2 → A1+A2 → E4 → I1+I2 → E3 → E1** — od najtańszych z największym zyskiem, z E1 na końcu jako najbardziej ryzykowną.
