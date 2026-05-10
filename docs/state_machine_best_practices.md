# State machines in embedded C/C++ — reliability and testability

> Applies to: bare-metal, FreeRTOS, Arduino framework, ESP-IDF, STM32 HAL.  
> Assumptions: no exceptions (`-fno-exceptions`), no RTTI, static memory allocation, deterministic execution time.

---

## 1. Closed set of states — `enum` instead of `int`

The first and most important rule: the state must be a type, not a magic number.

```c
/* ❌ BAD — nothing prevents state 99 */
int state = 0;

/* ✅ GOOD — the compiler knows every possible state */
typedef enum {
    STATE_IDLE,
    STATE_CONNECTING,
    STATE_CONNECTED,
    STATE_ERROR,
    STATE_COUNT  /* sentinel — enables array-based validation */
} State;
```

In C++ you can use `enum class` for stronger typing:

```cpp
enum class State : uint8_t {  /* uint8_t = RAM savings on small MCUs */
    Idle,
    Connecting,
    Connected,
    Error
};
```

**Why this matters in embedded:** Undefined enum values fall into the `default` case of a switch, which in well-written code either logs an error or halts the system — instead of silently corrupting state.

---

## 2. Explicit transitions — table or switch, never blind `if/else if`

### Variant A — transition table (deterministic timing, easy to verify)

A transition table reads like a specification and is trivial to review in code review.

```c
typedef enum {
    EVENT_CONNECT,
    EVENT_CONNECTED_OK,
    EVENT_TIMEOUT,
    EVENT_DISCONNECT,
    EVENT_COUNT
} Event;

/* STATE_INVALID = transition not allowed */
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
        /* Defence against out-of-bounds — critical in embedded */
        return current;
    }
    uint8_t next = TRANSITION_TABLE[current][event];
    if (next == STATE_INVALID) {
        /* Disallowed transition — ignore or log */
        return current;
    }
    return (State)next;
}
```

### Variant B — switch/case (readable for complex transitions)

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
            /* Unknown state — system error */
            break;
    }
    /* Disallowed transition — no state change */
    return current;
}
```

**Rule:** The transition function never calls HAL, UART, GPIO. It takes a state + event and returns a new state. That is all.

---

## 3. Pure transition function — separation of logic from effects

In embedded, "side effects" are: writing to a peripheral register, sending data over UART, turning on an LED, calling `HAL_GPIO_WritePin`. They should never be inside the transition logic — they make testing harder and create hidden dependencies.

```c
/* Transition logic — pure, no effects */
State sm_reduce(State current, Event event);

/* Effects — called by the outer layer after a state change */
void sm_on_entry(State state);
void sm_on_exit(State state);
```

The main loop or a FreeRTOS task ties these elements together:

```c
void sm_dispatch(SmContext *ctx, Event event) {
    State prev  = ctx->state;
    State next  = sm_reduce(prev, event);

    if (next != prev) {
        sm_on_exit(prev);       /* Cleanup on exit */
        ctx->state = next;
        sm_on_entry(next);      /* Initialisation of the new state */
        sm_log_transition(prev, event, next);  /* Observability */
    }
}
```

### Entry/exit actions

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

## 4. Impossible states do not exist in the type

Instead of storing multiple flags that can contradict each other, encode every possible context as a separate state.

```c
/* ❌ BAD — bool-hell, 2^3 = 8 combinations, most of them nonsensical */
typedef struct {
    bool is_loading;
    bool has_data;
    bool has_error;
} UiState;

/* ✅ GOOD — 4 states, each semantically complete */
typedef enum {
    UI_IDLE,
    UI_LOADING,
    UI_READY,       /* implies has_data == true */
    UI_ERROR        /* implies has_error == true */
} UiState;
```

For states that carry data, use a union:

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

**Rule:** If it makes no sense to store a piece of data in a given state, it should not be accessible. An untagged union in C requires discipline; in C++ you can use `std::variant` if the environment allows it.

---

## 5. ISR-safety — state machine and interrupts

The state machine should almost always be executed from a single context. Events from an ISR must go through a queue or a flag.

```c
/* In ISR — ONLY set a flag or push to a queue */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    /* ❌ DO NOT call sm_dispatch() from ISR */
    /* ✅ Instead: */
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(event_queue, &event_rx_complete, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/* In FreeRTOS task — event processing */
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

On bare-metal without an RTOS, use a `volatile` flag and process it in the main loop:

```c
volatile Event pending_event = EVENT_NONE;  /* set in ISR */

/* In main loop */
if (pending_event != EVENT_NONE) {
    Event e = pending_event;
    pending_event = EVENT_NONE;  /* Atomic on 8/32-bit MCU for uint */
    sm_dispatch(&ctx, e);
}
```

> **Note:** On MCUs with non-atomic writes (e.g. 8-bit AVR with a 16-bit variable) use `ATOMIC_BLOCK` or a critical section.

---

## 6. Observability — logging transitions

Every transition should be loggable without changing the logic. In embedded, logging typically goes to UART, RTT (Segger), or a circular buffer in RAM.

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

For production with a strict RAM budget: a circular buffer of the last N transitions.

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

After a crash or watchdog reset, this buffer (in non-volatile RAM or a `.noinit` section) allows you to reconstruct the sequence of events.

---

## 7. Testability — unit tests without hardware

The pure transition function (`sm_reduce`) requires no MCU, no HAL, no peripheral mock. Tests can be written and run on a PC (CMake + Unity / Catch2).

### Directory structure

```
project/
├── src/
│   ├── sm.c          ← transition logic (pure, no HAL)
│   ├── sm.h
│   └── sm_effects.c  ← on_entry / on_exit (with HAL, not unit-tested)
└── test/
    ├── test_sm.c     ← tests on PC
    └── CMakeLists.txt
```

### Example tests (Unity)

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
    /* IDLE cannot transition via CONNECTED_OK */
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
    /* From STATE_CONNECTED you cannot enter via CONNECT or TIMEOUT */
    TEST_ASSERT_EQUAL(STATE_CONNECTED, sm_reduce(STATE_CONNECTED, EVENT_CONNECT));
    TEST_ASSERT_EQUAL(STATE_CONNECTED, sm_reduce(STATE_CONNECTED, EVENT_TIMEOUT));
    TEST_ASSERT_EQUAL(STATE_CONNECTED, sm_reduce(STATE_CONNECTED, EVENT_CONNECTED_OK));
}
```

**Test pattern:** Given `(current_state, event)` → assert `next_state`. No hardware initialisation, no `HAL_Init()`, no `FreeRTOS_Init()`.

---

## 8. Watchdog integration

The state machine integrates naturally with the watchdog: every transition to a "healthy" state resets the WDT counter; getting stuck in one state for too long causes a system reset.

```c
void sm_on_entry(State state) {
    /* Reset watchdog on every transition — proof of activity */
    HAL_IWDG_Refresh(&hiwdg);

    switch (state) {
        /* ... */
    }
}
```

For long-lived states (e.g. `STATE_CONNECTED` for hours) the watchdog must be refreshed inside the task loop, not only on transition.

---

## Summary — checklist

| Property | Technique in C/C++ embedded |
|---|---|
| Closed set of states | `enum` / `enum class` + `STATE_COUNT` sentinel |
| Explicit transitions | Transition table or `switch/case` — every disallowed transition = no change |
| Pure transition function | `State sm_reduce(State, Event)` without HAL, without effects |
| Separated effects | `on_entry()` / `on_exit()` called by the dispatcher |
| Impossible states | Tagged union instead of multiple bool flags |
| ISR-safety | Events via queue (`xQueueSendFromISR`) or volatile flag |
| Observability | `sm_log_transition()` + history buffer in `.noinit` |
| Testability | `sm_reduce()` in a separate `.c`, Unity/Catch2 tests on PC without MCU |
| Watchdog | WDT reset in `on_entry()` / in the loop of long-lived states |

> **Golden rule of embedded:** If a transition between two states is not in the table or switch — it is impossible. Not "should not happen" — **impossible at runtime**.
