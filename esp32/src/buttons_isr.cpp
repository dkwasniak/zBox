#include "buttons_isr.h"
#include <Arduino.h>

Button buttons[BTN_COUNT] = {
    {BTN_A, "A", false, 0, 0, false, 0, 0, false},
    {BTN_B, "B", false, 0, 0, false, 0, 0, false},
    {BTN_C, "C(VOL-)", false, 0, 0, false, 0, 0, false},
    {BTN_D, "D(VOL+)", false, 0, 0, false, 0, 0, false},
};

void IRAM_ATTR btnISR(void *arg)
{
    Button *b = (Button *)arg;
    unsigned long now = millis();
    if (now - b->lastInterrupt > DEBOUNCE_MS)
    {
        b->pressed = true;
        b->lastInterrupt = now;
    }
}

void buttonsInit()
{
    for (int i = 0; i < BTN_COUNT; i++)
    {
        pinMode(buttons[i].pin, INPUT_PULLUP);
        attachInterruptArg(digitalPinToInterrupt(buttons[i].pin),
                           btnISR, &buttons[i], FALLING);
    }
}
