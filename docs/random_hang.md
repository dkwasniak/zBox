# Analiza i naprawa: losowe zamrożenie loop() podczas odtwarzania BT

## Objawy

- Muzyka gra dalej (audio task żyje)
- Przyciski nie reagują (loop() zamrożony)
- Animacje LED zatrzymują się
- **Freeze permanentny** — wymaga fizycznego resetu
- Brak interakcji użytkownika w momencie freeze

## Architektura tasków

```
Core 0: [BT stack — ciężkie przerwania RF] + [LED task prio 1, stack 2048B]
Core 1: [loop() prio 1] + [NFC task prio 1] + [audio task prio 2]
```

## Zidentyfikowane przyczyny

### Przyczyna 1 — LED task: za mały stack (2048 B) ← GŁÓWNA HIPOTEZA
- FastLED na ESP32 używa RMT peripheral + semaforu + wewnętrznej maszyny stanów
- 2048 B to za mało dla ramek stosu FastLED.show() + RMT ISR handler
- **Stack overflow nadpisuje sąsiadujące struktury heap (TCB innych tasków)**
- Wyjaśnia, dlaczego loop() i LED task zamrażają się równocześnie
- **Fix:** stack 2048 → 4096 B (lub 8192 jeśli HWM < 200)

### Przyczyna 2 — LED task na core 0: RMT vs BT A2DP
- FastLED.show() dla WS2812B przez RMT czeka na semafor sygnalizowany przez RMT ISR
- BT radio interrupt (core 0, wysoki priorytet) opóźnia lub zagłusza RMT ISR
- FastLED.show() może zablokować się permanentnie na semaforze
- Udokumentowany problem: FastLED issue #1438
- **Fix (fallback):** przepnij LED task na core 1

### Przyczyna 3 — LED shared vars bez volatile (race condition)
- `ledMode`, `ledAnimStep`, `ledLastUpdate`, `ledVolumeShowTime`, `ledBootStep`, `ledSyncLit`
- Modyfikowane z audio task (core 1), czytane/modyfikowane z LED task (core 0)
- Brak `volatile` → kompilator może cache'ować wartości
- **Nie powoduje freezy**, ale może powodować błędne animacje
- **Fix:** dodano `volatile`

## Zastosowane zmiany (branch `bluetooth`, main.cpp)

### 1. volatile na LED shared vars (linia 152)
```cpp
volatile LedMode ledMode = LED_OFF;
volatile unsigned long ledLastUpdate = 0;
volatile int ledAnimStep = 0;
volatile int ledBootStep = -1;
volatile unsigned long ledVolumeShowTime = 0;
volatile int ledSyncLit = 0;
```

### 2. Stack LED task 2048 → 4096 (linia 303)
```cpp
xTaskCreatePinnedToCore(ledTaskFunc, "led", 4096, NULL, 1, &ledTaskHandle, 0);
```

### 3. Heartbeat w ledTaskFunc PRZED FastLED.show() (linia ~442)
```cpp
static unsigned long lastLedHeartbeat = 0;
// na początku for(;;), przed animacjami:
if (now - lastLedHeartbeat > 5000) {
    lastLedHeartbeat = now;
    LOG("[LED] alive mode=%d core=%d hwm=%u\n",
        (int)ledMode, xPortGetCoreID(), uxTaskGetStackHighWaterMark(NULL));
}
```

### 4. Diagnostyka HWM co 30s w loop() (linia ~2131)
```cpp
static unsigned long lastDiag = 0;
if (millis() - lastDiag > 30000) {
    lastDiag = millis();
    LOG("[DIAG] core=%d Stack HWM: loop=%u led=%u audio=%u\n",
        xPortGetCoreID(),
        uxTaskGetStackHighWaterMark(NULL),
        ledTaskHandle ? uxTaskGetStackHighWaterMark(ledTaskHandle) : 0,
        audioTaskHandle ? uxTaskGetStackHighWaterMark(audioTaskHandle) : 0);
}
```

## Interpretacja logów po freeze

| Wynik | Wniosek | Następny krok |
|-------|---------|---------------|
| `[LED] alive` znika razem z `[LOOP] alive` | Heap corruption (stack overflow) | stack 4096 → 8192 |
| `[LED] alive` bije, `[LOOP] alive` znika | Izolowany hang w loop() | Szukać blokującego calla w loop() |
| `[LED] alive` znika, `[LOOP] alive` bije | FastLED.show() wisi (RMT/BT conflict) | Przepnij LED task na core 1 |
| LED HWM < 200 | Stack za mały | 4096 → 8192 |
| LED HWM > 1000 i wciąż hang | Stack OK, problem gdzie indziej | Analiza RMT/BT conflict |

## Fallback jeśli diagnostyka wskaże RMT/BT conflict

Przepnąć LED task na **core 1** (ostatni argument):
```cpp
// main.cpp:303
xTaskCreatePinnedToCore(ledTaskFunc, "led", 4096, NULL, 1, &ledTaskHandle, 1); // core 1 (było 0)
```
Eliminuje konflikt BT radio interrupt (core 0) vs RMT ISR (core 0).

## Procedura weryfikacji

1. `cd esp32 && pio run -t upload && pio device monitor`
2. BT connect → postaw figurkę → muzyka → NIE dotykaj przycisków przez 30-60 min
3. Obserwuj:
   - `[LED] alive mode=X core=0 hwm=Y` co 5s
   - `[DIAG] core=1 Stack HWM: loop=A led=B audio=C` co 30s
   - `[LOOP] alive ...` co 2s
4. Przy freeze: zapisz ostatnie 2 minuty logów

## Status

- [x] volatile na shared vars
- [x] Stack 2048 → 4096
- [x] Heartbeat w LED task przed show()
- [x] Diagnostyka HWM w loop()
- [ ] Weryfikacja w terenie (30-60 min bez interakcji)
- [ ] Ewentualny fallback: core 1 lub stack 8192
