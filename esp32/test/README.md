# MusicBox — dokumentacja testów

## Spis treści

1. [Architektura testów](#architektura)
2. [Etap 1 — Native unit tests](#etap-1)
3. [Etap 2 — Boot sequence & timing](#etap-2)
4. [Etap 3 — Heartbeat & DIAG](#etap-3)
5. [Etap 4 — Tester firmware](#etap-4)
6. [Etap 5 — Button automation](#etap-5)
7. [Etap 6 — Manual NFC tests](#etap-6)
8. [Infrastruktura](#infrastruktura)
9. [Sprzęt testowy](#sprzet)
10. [Znane zachowania hardware](#zachowania)

---

## Architektura testów {#architektura}

Trzy poziomy testów:

```
┌─────────────────────────────────────────────────────────┐
│  Etap 1: Native unit tests                              │
│  PlatformIO + Unity, C++, BEZ hardware                  │
│  esp32/test/test_*/                                     │
├─────────────────────────────────────────────────────────┤
│  Etapy 2-3: Serial integration tests                    │
│  Python + pyserial, obserwacja logów DUT przez USB      │
│  esp32/test/integration/test_boot_sequence.py                │
│  esp32/test/integration/test_timing.py                       │
│  esp32/test/integration/test_heartbeat.py                    │
├─────────────────────────────────────────────────────────┤
│  Etapy 4-6: Hardware automation + manual                │
│  Drugi ESP32-S3 symuluje przyciski, figurka NFC         │
│  esp32/test/integration/test_buttons.py                      │
│  esp32/test/integration/test_nfc_playback.py                 │
└─────────────────────────────────────────────────────────┘
```

---

## Etap 1 — Native unit tests {#etap-1}

### Wymagania

```bash
cd esp32
pio platform install native   # jednorazowo
```

### Uruchomienie

```bash
cd esp32
pio test -e native -v
```

### Co jest testowane

#### `test_uid_format` (7 testów)

Testuje `uidToString()` z `main.cpp:271` — konwersja tablicy bajtów NFC UID na string.

| Test | Wejście | Oczekiwany wynik |
|------|---------|-----------------|
| `test_uid_4_bytes` | `{0x04, 0xA3, 0xB2, 0xC1}` | `"04:A3:B2:C1"` |
| `test_uid_7_bytes` | 7 bajtów | `"04:A3:...:80"` |
| `test_uid_lowercase_uppercased` | `{0xab, 0xcd}` | `"AB:CD"` |
| `test_uid_zero_byte_padded` | `{0x00, 0x05}` | `"00:05"` (nie `"0:5"`) |
| `test_uid_ff_byte` | `{0xFF, 0xFF}` | `"FF:FF"` |
| `test_uid_single_byte` | `{0x05}` | `"05"` |
| `test_uid_all_zeros` | `{0,0,0,0}` | `"00:00:00:00"` |

**Uwaga:** `test_uid_zero_byte_padded` to guard regresyjny. `String(0x05, HEX)` w Arduino zwraca `"5"` (bez zera), padding `"0"` jest dodawany ręcznie w `uidToString()`. Jeśli ta logika zostanie usunięta przez refaktor, test to wykryje.

#### `test_volume` (16 testów)

Testuje logikę głośności (`main.cpp:684-698`) i `batteryBars()` (`main.cpp:1790`).

**Głośność** (`BT_VOL_STEP=5, MIN=0, MAX=100`):

| Test | Operacja | Stan wejściowy | Oczekiwany wynik |
|------|----------|----------------|-----------------|
| `test_vol_up_normal` | `volumeUp` | 50 | 55 |
| `test_vol_up_clamps_at_100` | `volumeUp` | 100 | 100 (clamp) |
| `test_vol_near_max` | `volumeUp` | 98 | 100 (clamp) |
| `test_vol_down_normal` | `volumeDown` | 50 | 45 |
| `test_vol_down_clamps_at_0` | `volumeDown` | 0 | 0 (clamp) |
| `test_vol_near_min` | `volumeDown` | 2 | 0 (clamp) |

**Bateria** (progi z krzywej rozładowania Li-Po 1S):

| Test | Napięcie | Oczekiwane paski |
|------|----------|-----------------|
| `test_battery_5bars` | 4.20V | 5 |
| `test_battery_5bars_edge` | 4.05V | 5 (granica) |
| `test_battery_4bars` | 3.95V | 4 |
| `test_battery_4bars_edge` | 3.90V | 4 (granica) |
| `test_battery_3bars` | 3.85V | 3 |
| `test_battery_3bars_edge` | 3.80V | 3 (granica) |
| `test_battery_2bars` | 3.75V | 2 |
| `test_battery_2bars_edge` | 3.70V | 2 (granica) |
| `test_battery_1bar` | 3.50V | 1 |
| `test_battery_boundary_below_390` | 3.899V | 3 (nie 4) |

#### `test_mapping` (8 testów)

Replikuje logikę `loadMappings()` (`main.cpp:739`) z ArduinoJson v7, bez SD (dane z JSON string).

| Test | Scenariusz | Oczekiwany wynik |
|------|-----------|-----------------|
| `test_valid_single_mapping` | 1 figurka w JSON | `size=1, returns true` |
| `test_valid_multiple_mappings` | 2 figurki | `size=2, returns true` |
| `test_empty_figurines_object` | `{"figurines":{}}` | `size=0, returns true` |
| `test_invalid_json` | `{not valid` | `returns false` |
| `test_missing_figurines_key` | `{"other":{}}` | `size=0, returns true`* |
| `test_lookup_known_uid` | znany UID | `find() != end()` |
| `test_lookup_unknown_uid` | nieznany UID | `find() == end()` |
| `test_filename_value_correct` | znany UID | `value == "abc.mp3"` |

*Firmware nie sprawdza obecności klucza `figurines` — zwraca `true` z pustą mapą. Test dokumentuje to zachowanie.

---

## Etap 2 — Boot sequence & timing {#etap-2}

### Wymagania

```bash
cd esp32/test
pip install -r requirements.txt
```

DUT (Lolin D32 Pro) podłączony przez USB, firmware wgrany.

### Uruchomienie

```bash
cd esp32/test
pytest integration/test_boot_sequence.py integration/test_timing.py \
  -v --port /dev/cu.usbserial-10
```

### Co jest testowane

#### `test_boot_sequence.py` (5 testów)

Weryfikuje że wszystkie komunikaty boot pojawiają się we właściwej kolejności:

```
=== MusicBox ===
[T+  0] Boot start
[T+  N] GPIO ready
[T+  N] SD OK|FAIL
[BOOT] sync_pending flag: 0
--- Normal mode
[T+  N] NFC OK|FAIL
[T+  N] Mappings loaded (N)
[T+  N] BT A2DP starting -> JBL GO 2
```

| Test | Co weryfikuje |
|------|--------------|
| `test_boot_sequence_order` | Cała sekwencja w kolejności (timeout 30s) |
| `test_sd_reports_status` | Log SD zawiera `OK` lub `FAIL` (nie milczy) |
| `test_nfc_reports_status` | Log NFC zawiera `OK` lub `FAIL` |
| `test_mappings_count_logged` | `Mappings loaded (N)` — N jest liczbą |
| `test_bt_starting_with_name` | Log zawiera konkretną nazwę `JBL GO 2` |

#### `test_timing.py` (4 testy + 1 skipped)

Weryfikuje że inicjalizacja podzespołów mieści się w progach czasowych. Wartości T+ mierzone są od `bootStart` (po `handleWakeFromDeepSleep()`).

| Test | Próg | Empiryczna wartość |
|------|------|-------------------|
| `test_gpio_ready_within_200ms` | ≤ 200ms | ~5ms |
| `test_sd_init_within_500ms` | ≤ 500ms | ~24ms |
| `test_nfc_init_within_2500ms` | ≤ 2500ms | ~1576ms |
| `test_bt_start_within_5000ms` | ≤ 5000ms | ~3638ms |
| `test_boot_to_play_under_10s` | — | wymaga `--run-hardware` |

---

## Etap 3 — Heartbeat & DIAG {#etap-3}

### Uruchomienie

```bash
# Bez soak (szybki, ~35s)
pytest integration/test_heartbeat.py -v --port /dev/cu.usbserial-10 -m "not soak"

# Z soak (60s bez zawieszenia)
pytest integration/test_heartbeat.py -v --port /dev/cu.usbserial-10 -m soak
```

### Ważne: brak resetu w testach heartbeat

Testy heartbeat **nie resetują urządzenia** — obserwują działający system po boot testach. Powód: wielokrotne szybkie resety powodują, że JBL GO 2 przestaje szybko reconnectować (BT connection time 6→40+s), przez co `setup()` blokuje na `a2dp.begin()` na czas przekraczający timeouty testów.

Heartbeat testy są zależne od tego, że poprzednie testy zostawiły urządzenie w stanie `loop()` działa.

### Co jest testowane

| Test | Pattern | Timeout | Co weryfikuje |
|------|---------|---------|--------------|
| `test_loop_heartbeat_appears` | `[LOOP] alive` | 10s | `loop()` nie jest zawieszony |
| `test_loop_heartbeat_repeats_3x` | `[LOOP] alive` ×3 | 25s | 3 różne linie (nie ta sama) |
| `test_nfc_task_heartbeat` | `[NFC] alive hwm=N err=N` | 10s | NFC task żyje |
| `test_diag_hwm_appears` | `[DIAG] HWM` | 35s | Stack monitor uruchomiony |
| `test_diag_all_tasks_above_512` | HWM loop/led/audio/nfc | 35s | Żaden task nie grozi stack overflow |
| `test_diag_heap_appears` | `[DIAG] heap free=N` | 35s | Heap monitor działa |
| `test_no_freeze_60s` | `[LOOP] alive` co ≤15s | 60s | Brak zawieszenia przez minutę |

---

## Etap 4 — Tester firmware {#etap-4}

### Opis

Osobny firmware dla drugiego ESP32-S3 (N16R8) który fizycznie symuluje przyciski DUT przez GPIO.

### Schemat połączeń

```
Tester ESP32-S3          DUT Lolin D32 Pro
  GPIO4  (OUT_A) ──────  GPIO32 (BTN_A, długie = bateria)
  GPIO5  (OUT_B) ──────  GPIO33 (BTN_B, wolny)
  GPIO6  (OUT_C) ──────  GPIO25 (BTN_C, VOL- / sleep / sync)
  GPIO7  (OUT_D) ──────  GPIO26 (BTN_D, VOL+ / wake)
  GND            ──────  GND   (wspólna masa — obowiązkowe!)
```

### Zasada działania

- **Naciśnięcie** = `pinMode(pin, OUTPUT)` + `digitalWrite(pin, LOW)` — DUT INPUT_PULLUP widzi LOW
- **Puszczenie** = `pinMode(pin, INPUT)` — Hi-Z, pullup DUT ciągnie do HIGH
- **NIGDY** `OUTPUT HIGH` — uniknięcie konfliktu z pullupem DUT

### Protokół serial (115200)

| Komenda | Efekt |
|---------|-------|
| `PRESS A 2000\n` | Wciśnij BTN_A na 2000ms, puść |
| `PRESS_COMBO CD 2500\n` | Wciśnij BTN_C + BTN_D jednocześnie na 2500ms |
| `RELEASE ALL\n` | Puść wszystkie piny (Hi-Z) |
| `PING\n` | Odpowiada `PONG\n` |

Odpowiedzi: `OK\n` (sukces) lub `ERR reason\n` (błąd).
Safety timeout: 10s bez komendy → automatyczny `RELEASE ALL`.

### Wgranie firmware

```bash
cd esp32/tester
pio run -t upload

# Weryfikacja
python3 -c "
import serial, time
s = serial.Serial('/dev/cu.usbmodem*', 115200, timeout=2)
s.write(b'PING\n')
print(s.readline())  # powinno: b'PONG\n'
s.close()
"
```

---

## Etap 5 — Button automation tests {#etap-5}

### Wymagania

- Tester ESP32-S3 podłączony i wgrany (etap 4)
- Kable między testerem a DUT zgodnie ze schematem
- DUT przez USB na `--port`
- Tester przez USB na `--tester-port`

### Uruchomienie

```bash
pytest integration/test_buttons.py -v \
  --port /dev/cu.usbserial-10 \
  --tester-port /dev/cu.usbmodem<N>
```

Wszystkie testy mają marker `@requires_tester` — bez `--tester-port` są automatycznie pomijane.

Każdy test czeka na `BT A2DP starting` przed wykonaniem akcji (pewność że device booted).

### Co jest testowane

| Test | Akcja | Weryfikowany log |
|------|-------|-----------------|
| `test_volume_up_logs` | Krótkie BTN_D (100ms) | `[VOL]` |
| `test_volume_down_logs` | Krótkie BTN_C (100ms) | `[VOL]` |
| `test_battery_long_press_a` | Długie BTN_A (2500ms) | `[BAT] Voltage:` |
| `test_battery_voltage_range` | Długie BTN_A (2500ms) | napięcie 3.0V–4.5V |
| `test_deep_sleep_trigger` | Długie BTN_C (2500ms) | `[SLEEP]` |
| `test_sync_mode_trigger` | Combo BTN_C+D (2500ms) | `Sync flag written` → restart → `SYNC MODE` |

---

## Etap 6 — Manual NFC tests {#etap-6}

### Uruchomienie

```bash
pytest integration/test_nfc_playback.py -v \
  --port /dev/cu.usbserial-10 \
  --run-manual
```

Bez `--run-manual` testy są pomijane automatycznie.

### Co jest testowane

Testy proszą o fizyczną interakcję przez `input()` przed każdą weryfikacją.

| Test | Akcja użytkownika | Weryfikowany log |
|------|------------------|-----------------|
| `test_nfc_place_triggers_playback` | Postaw figurkę | `startPlayback uid=...` + `PLAYBACK START` |
| `test_uid_format_valid` | Postaw figurkę | UID pasuje do `[0-9A-F]{2}(:[0-9A-F]{2}){3,6}` |
| `test_nfc_remove_stops_playback` | Zabierz figurkę | `stopPlayback` w 5s |
| `test_audio_telemetry` | Figurka gra muzykę | `[AUDIO_TEL] SD=N B/s` |

---

## Infrastruktura {#infrastruktura}

### `serial_harness.py`

Klasa `SerialHarness` — asynchroniczny odczyt serial z wyszukiwaniem wzorców.

| Metoda | Opis |
|--------|------|
| `wait_for_line(pattern, timeout_s)` | Czeka na linię pasującą do regex. Skanuje od pozycji 0 bufora. |
| `wait_for_nth_occurrence(pattern, n, timeout_s)` | Czeka na N różnych wystąpień wzorca (śledzi pozycję — nie zwraca tej samej linii). |
| `wait_for_sequence(patterns, timeout_s)` | Czeka na listę wzorców w kolejności. |
| `reset_device()` | DTR pulse: `setDTR(False)` → 100ms → `setDTR(True)` → 100ms. Resetuje ESP32 przez pin EN. |
| `flush()` | Czyści bufor i event. Używaj przed `reset_device()` żeby uniknąć starych linii. |

Bufor: `deque(maxlen=2000)`. Reader thread działa w tle przez cały czas.

### `button_tester.py`

Klasa `ButtonTester` — sterowanie tester ESP32-S3 przez serial.

| Metoda | Komenda wysyłana |
|--------|-----------------|
| `press(btn, duration_ms)` | `PRESS A 2000\n` |
| `press_combo(btns, duration_ms)` | `PRESS_COMBO CD 2500\n` |
| `release_all()` | `RELEASE ALL\n` |

### `conftest.py` — fixtures i markery

**Fixtures:**

| Fixture | Scope | Opis |
|---------|-------|------|
| `serial_harness` | session | Jeden SerialHarness na całą sesję pytest. Nie resetuje przy starcie. |
| `button_tester` | session | ButtonTester, skip jeśli `--tester-port` nie podano. |
| `reset_esp` | function | Flush + reset DUT przed testem (dla testów które tego wymagają). |

**Markery:**

| Marker | Jak uruchomić | Opis |
|--------|--------------|------|
| `soak` | `-m soak` | Długi test (>60s) |
| `requires_tester` | `--tester-port /dev/cu.usbmodem*` | Wymaga tester ESP32-S3 |
| `requires_hardware` | `--run-hardware` | Wymaga JBL + figurki |
| `manual` | `--run-manual` | Wymaga fizycznej interakcji |

---

## Sprzęt testowy {#sprzet}

### Wymagane do etapów 1

Nic — testy kompilują się i uruchamiają natywnie na PC.

### Wymagane do etapów 2-3

- DUT: Lolin D32 Pro z wgranym firmware MusicBox
- Kabel USB → `/dev/cu.usbserial-10` (CH340)
- Karta SD z `data/mappings.json`
- JBL GO 2 włączony (testy czekają na boot, BT connect jest w tle)

### Wymagane do etapów 4-5

Jak wyżej, plus:
- Tester: ESP32-S3-DevKitC-1 (N16R8) z wgranym `esp32/tester/`
- Kabel USB testera → `/dev/cu.usbmodem*`
- 4 kable GPIO między testerem a DUT (patrz schemat w etapie 4)
- Wspólna masa GND — obowiązkowe

### Wymagane do etapu 6

Jak etapy 2-3, plus figurka z naklejką NFC (dowolna z `data/mappings.json`).

---

## Znane zachowania hardware {#zachowania}

### BT reconnect czas

JBL GO 2 reconnectuje w 6-9s od resetu (zmiennie). Po kilku kolejnych szybkich resetach czas reconnectu rośnie powyżej 10s. Dlatego:
- Testy heartbeat **nie resetują** urządzenia
- Testy timing mają próg BT start na 5000ms (nie 3000ms)
- Nie uruchamiaj wielu resetów z rzędu bez przerwy między nimi

### Figurka na padzie podczas testów

Podczas testów boot logi pokazują `NFC pre-scan: 3C:26:D6:05` — figurka leży na padzie i muzyka startuje automatycznie. Jest to normalne i nie zakłóca testów 1-3. Testy przycisków (etap 5) nie wymagają figurki.

### IDLE_TIMEOUT

Po 10 minutach bez odtwarzania urządzenie wchodzi w deep sleep. Jeśli testy trwają > 10 min bez figurki, urządzenie może zasnąć i testy heartbeat przestaną dostawać odpowiedzi. Rozwiązanie: trzymaj figurkę na padzie lub uruchamiaj testy z mniejszymi przerwami.
