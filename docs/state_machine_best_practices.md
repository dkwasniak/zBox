# Maszyna stanów w embedded C/C++ — niezawodność i testowalność

> Dotyczy: bare-metal, FreeRTOS, Arduino framework, ESP-IDF, STM32 HAL.  
> Założenia: brak wyjątków (`-fno-exceptions`), brak RTTI, statyczna alokacja pamięci, deterministyczny czas wykonania.

---

## 1. Zamknięty zbiór stanów — `enum` zamiast `int`

Pierwsza i najważniejsza zasada: stan musi być typem, nie magiczną liczbą.

```c
/* ❌ ŹLE — nic nie chroni przed stanem 99 */
int state = 0;

/* ✅ DOBRZE — kompilator zna każdy możliwy stan */
typedef enum {
    STATE_IDLE,
    STATE_CONNECTING,
    STATE_CONNECTED,
    STATE_ERROR,
    STATE_COUNT  /* sentinel — pozwala na walidację tablicową */
} State;
```

W C++ można użyć `enum class` dla silniejszego typowania:

```cpp
enum class State : uint8_t {  /* uint8_t = oszczędność RAM na małych MCU */
    Idle,
    Connecting,
    Connected,
    Error
};
```

**Dlaczego to ważne w embedded:** Niezdefiniowane wartości enuma wchodzą do switcha jako `default`, który w dobrze napisanym kodzie albo loguje błąd, albo zatrzymuje system — zamiast cicho korumpować stan.

---

## 2. Jawne przejścia — tabela lub switch, nigdy `if/else if` na ślepo

### Wariant A — tabela przejść (deterministyczny czas, łatwa weryfikacja)

Tabela przejść jest czytelna jak specyfikacja i trywialna do przejrzenia code review.

```c
typedef enum {
    EVENT_CONNECT,
    EVENT_CONNECTED_OK,
    EVENT_TIMEOUT,
    EVENT_DISCONNECT,
    EVENT_COUNT
} Event;

/* STATE_INVALID = przejście niedozwolone */
#define STATE_INVALID 0xFF

static const uint8_t TRANSITION_TABLE[STATE_COUNT][EVENT_COUNT] = {
    /*                  CONNECT            CONNECTED_OK       TIMEOUT          DISCONNECT     */
    [STATE_IDLE]      = { STATE_CONNECTING,  STATE_INVALID,     STATE_INVALID,   STATE_INVALID  },
    [STATE_CONNECTING]= { STATE_INVALID,     STATE_CONNECTED,   STATE_ERROR,     STATE_IDLE     },
    [STATE_CONNECTED] = { STATE_INVALID,     STATE_INVALID,     STATE_INVALID,   STATE_IDLE     },
    [STATE_ERROR]     = { STATE_CONNECTING,  STATE_INVALID,     STATE_INVALID,   STATE_IDLE     },
};

State sm_process(State current, Event event) {
    if (current >= STATE_COUNT || event >= EVENT_COUNT) {
        /* Obrona przed out-of-bounds — krytyczne w embedded */
        return current;
    }
    uint8_t next = TRANSITION_TABLE[current][event];
    if (next == STATE_INVALID) {
        /* Niedozwolone przejście — zignoruj lub zaloguj */
        return current;
    }
    return (State)next;
}
```

### Wariant B — switch/case (czytelny przy złożonych przejściach)

```c
State sm_process(State current, Event event) {
    switch (current) {
        case STATE_IDLE:
            if (event == EVENT_CONNECT) return STATE_CONNECTING;
            break;

        case STATE_CONNECTING:
            if (event == EVENT_CONNECTED_OK) return STATE_CONNECTED;
            if (event == EVENT_TIMEOUT)      return STATE_ERROR;
            if (event == EVENT_DISCONNECT)   return STATE_IDLE;
            break;

        case STATE_CONNECTED:
            if (event == EVENT_DISCONNECT)   return STATE_IDLE;
            break;

        case STATE_ERROR:
            if (event == EVENT_CONNECT)      return STATE_CONNECTING;
            if (event == EVENT_DISCONNECT)   return STATE_IDLE;
            break;

        default:
            /* Nieznany stan — błąd systemu */
            break;
    }
    /* Niedozwolone przejście — brak zmiany stanu */
    return current;
}
```

**Zasada:** Funkcja przejścia nigdy nie wywołuje HAL, UART, GPIO. Przyjmuje stan + zdarzenie, zwraca nowy stan. To wszystko.

---

## 3. Czysta funkcja przejścia — separacja logiki od efektów

W embedded "efekty uboczne" to: zapis do rejestru peryferyjnego, wysłanie danych po UART, zapalenie diody, wywołanie `HAL_GPIO_WritePin`. Nigdy nie powinny znajdować się w logice przejścia — utrudniają testowanie i tworzą ukryte zależności.

```c
/* Logika przejścia — czysta, bez efektów */
State sm_reduce(State current, Event event);

/* Efekty — wywoływane przez warstwę zewnętrzną po zmianie stanu */
void sm_on_entry(State state);
void sm_on_exit(State state);
```

Pętla główna lub task FreeRTOS łączy te elementy:

```c
void sm_dispatch(SmContext *ctx, Event event) {
    State prev  = ctx->state;
    State next  = sm_reduce(prev, event);

    if (next != prev) {
        sm_on_exit(prev);       /* Sprzątanie po wyjściu */
        ctx->state = next;
        sm_on_entry(next);      /* Inicjalizacja nowego stanu */
        sm_log_transition(prev, event, next);  /* Obserwowalność */
    }
}
```

### Akcje wejścia/wyjścia

```c
void sm_on_entry(State state) {
    switch (state) {
        case STATE_CONNECTING:
            timer_start(TIMER_CONNECTION, TIMEOUT_MS);
            led_set(LED_STATUS, LED_BLINK_FAST);
            break;
        case STATE_CONNECTED:
            led_set(LED_STATUS, LED_ON);
            break;
        case STATE_ERROR:
            led_set(LED_STATUS, LED_BLINK_SLOW);
            error_counter_increment();
            break;
        default:
            break;
    }
}

void sm_on_exit(State state) {
    switch (state) {
        case STATE_CONNECTING:
            timer_stop(TIMER_CONNECTION);
            break;
        default:
            break;
    }
}
```

---

## 4. Niemożliwe stany nie istnieją w typie

Zamiast przechowywać wiele flag, które mogą wchodzić w sprzeczność, koduj każdy możliwy kontekst jako oddzielny stan.

```c
/* ❌ ŹLE — bool-hell, 2^3 = 8 kombinacji, większość bezsensowna */
typedef struct {
    bool is_loading;
    bool has_data;
    bool has_error;
} UiState;

/* ✅ DOBRZE — 4 stany, każdy semantycznie kompletny */
typedef enum {
    UI_IDLE,
    UI_LOADING,
    UI_READY,       /* implies has_data == true */
    UI_ERROR        /* implies has_error == true */
} UiState;
```

Dla stanów niosących dane użyj unii:

```c
typedef struct {
    State state;
    union {
        struct { uint32_t retry_count; } connecting;
        struct { uint8_t data[64]; uint16_t len; } connected;
        struct { uint32_t error_code; } error;
    } ctx;
} SmContext;
```

**Zasada:** Jeśli danej nie ma sensu przechowywać w tym stanie, nie powinna być dostępna. Unia bez tagu w C wymaga dyscypliny; w C++ można użyć `std::variant` jeśli środowisko na to pozwala.

---

## 5. ISR-safety — maszyna stanów a przerwania

Maszynę stanów prawie zawsze należy wykonywać z jednego kontekstu. Zdarzenia z ISR muszą przechodzić przez kolejkę lub flagę.

```c
/* W ISR — TYLKO ustawienie flagi lub push do kolejki */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    /* ❌ NIE wywołuj sm_dispatch() z ISR */
    /* ✅ Zamiast tego: */
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(event_queue, &event_rx_complete, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/* W tasku FreeRTOS — przetwarzanie zdarzeń */
void sm_task(void *arg) {
    SmContext ctx = { .state = STATE_IDLE };
    Event event;

    for (;;) {
        if (xQueueReceive(event_queue, &event, portMAX_DELAY) == pdTRUE) {
            sm_dispatch(&ctx, event);
        }
    }
}
```

Na bare-metal bez RTOS, użyj `volatile` flagi i przetwarzaj w pętli głównej:

```c
volatile Event pending_event = EVENT_NONE;  /* ustawiana w ISR */

/* W main loop */
if (pending_event != EVENT_NONE) {
    Event e = pending_event;
    pending_event = EVENT_NONE;  /* Atomiczne na 8/32-bit MCU dla uint */
    sm_dispatch(&ctx, e);
}
```

> **Uwaga:** Na MCU z zapisem nieatomicznym (np. 8-bit AVR z 16-bit zmienną) użyj `ATOMIC_BLOCK` lub sekcji krytycznej.

---

## 6. Obserwowalność — logowanie przejść

Każde przejście powinno być logowalne bez zmiany logiki. W embedded logowanie trafia najczęściej do UART, RTT (Segger), lub bufora cyklicznego w RAM.

```c
static const char *STATE_NAMES[] = {
    "IDLE", "CONNECTING", "CONNECTED", "ERROR"
};
static const char *EVENT_NAMES[] = {
    "CONNECT", "CONNECTED_OK", "TIMEOUT", "DISCONNECT"
};

void sm_log_transition(State from, Event event, State to) {
#ifdef DEBUG
    printf("[SM] %s + %s -> %s\n",
           STATE_NAMES[from],
           EVENT_NAMES[event],
           STATE_NAMES[to]);
#endif
}
```

Dla produkcji z rygorystycznym budżetem RAM: bufor cykliczny ostatnich N przejść.

```c
#define SM_HISTORY_SIZE 8

typedef struct {
    uint8_t from;
    uint8_t event;
    uint8_t to;
} SmRecord;

static SmRecord sm_history[SM_HISTORY_SIZE];
static uint8_t  sm_history_idx = 0;

void sm_record(State from, Event event, State to) {
    sm_history[sm_history_idx % SM_HISTORY_SIZE] = (SmRecord){from, event, to};
    sm_history_idx++;
}
```

Po crashu lub watchdog resecie ten bufor (w RAM nieulotnym lub sekcji `.noinit`) pozwala zrekonstruować sekwencję zdarzeń.

---

## 7. Testowalność — unit testy bez hardware

Czysta funkcja przejścia (`sm_reduce`) nie wymaga żadnego MCU, żadnego HAL, żadnego mocka peryferyjnego. Testy można pisać i uruchamiać na PC (CMake + Unity / Catch2).

### Struktura katalogów

```
project/
├── src/
│   ├── sm.c          ← logika przejść (czysta, bez HAL)
│   ├── sm.h
│   └── sm_effects.c  ← on_entry / on_exit (z HAL, nie testowane jednostkowo)
└── test/
    ├── test_sm.c     ← testy na PC
    └── CMakeLists.txt
```

### Przykładowe testy (Unity)

```c
#include "unity.h"
#include "sm.h"

void test_idle_connect_transitions_to_connecting(void) {
    State result = sm_reduce(STATE_IDLE, EVENT_CONNECT);
    TEST_ASSERT_EQUAL(STATE_CONNECTING, result);
}

void test_connecting_timeout_transitions_to_error(void) {
    State result = sm_reduce(STATE_CONNECTING, EVENT_TIMEOUT);
    TEST_ASSERT_EQUAL(STATE_ERROR, result);
}

void test_invalid_transition_returns_current_state(void) {
    /* IDLE nie może przejść przez CONNECTED_OK */
    State result = sm_reduce(STATE_IDLE, EVENT_CONNECTED_OK);
    TEST_ASSERT_EQUAL(STATE_IDLE, result);
}

void test_full_happy_path(void) {
    State s = STATE_IDLE;
    s = sm_reduce(s, EVENT_CONNECT);
    TEST_ASSERT_EQUAL(STATE_CONNECTING, s);
    s = sm_reduce(s, EVENT_CONNECTED_OK);
    TEST_ASSERT_EQUAL(STATE_CONNECTED, s);
    s = sm_reduce(s, EVENT_DISCONNECT);
    TEST_ASSERT_EQUAL(STATE_IDLE, s);
}

void test_all_invalid_transitions_from_connected(void) {
    /* Z STATE_CONNECTED nie można wejść przez CONNECT ani TIMEOUT */
    TEST_ASSERT_EQUAL(STATE_CONNECTED, sm_reduce(STATE_CONNECTED, EVENT_CONNECT));
    TEST_ASSERT_EQUAL(STATE_CONNECTED, sm_reduce(STATE_CONNECTED, EVENT_TIMEOUT));
    TEST_ASSERT_EQUAL(STATE_CONNECTED, sm_reduce(STATE_CONNECTED, EVENT_CONNECTED_OK));
}
```

**Wzorzec testowy:** Given `(current_state, event)` → assert `next_state`. Bez inicjalizacji sprzętu, bez `HAL_Init()`, bez `FreeRTOS_Init()`.

---

## 8. Watchdog integration

Maszyna stanów naturalnie integruje się z watchdogiem: każde przejście do stanu "zdrowego" resetuje licznik WDT; utknięcie w jednym stanie przez zbyt długo powoduje reset systemu.

```c
void sm_on_entry(State state) {
    /* Reset watchdoga tylko przy przejściach — dowód aktywności */
    HAL_IWDG_Refresh(&hiwdg);

    switch (state) {
        /* ... */
    }
}
```

Dla stanów długotrwałych (np. `STATE_CONNECTED` przez godziny) watchdog musi być odświeżany w pętli zadania, nie tylko przy przejściu.

---

## Podsumowanie — checklista

| Właściwość | Technika w C/C++ embedded |
|---|---|
| Zamknięty zbiór stanów | `enum` / `enum class` + `STATE_COUNT` sentinel |
| Jawne przejścia | Tabela przejść lub `switch/case` — każde niedozwolone = brak zmiany |
| Czysta funkcja przejścia | `State sm_reduce(State, Event)` bez HAL, bez efektów |
| Oddzielone efekty | `on_entry()` / `on_exit()` wywoływane przez dispatcher |
| Niemożliwe stany | Unia z tagiem zamiast wielu flag bool |
| ISR-safety | Zdarzenia przez kolejkę (`xQueueSendFromISR`) lub volatile flagę |
| Obserwowalność | `sm_log_transition()` + bufor historii w `.noinit` |
| Testowalność | `sm_reduce()` w oddzielnym `.c`, testy Unity/Catch2 na PC bez MCU |
| Watchdog | Reset WDT w `on_entry()` / w pętli długotrwałych stanów |

> **Złota zasada embedded:** Jeśli przejście między dwoma stanami nie jest w tabeli lub switchu — jest niemożliwe. Nie "nie powinno się zdarzać" — **niemożliwe w runtime**.