// Tester ESP32-S3 — simulates DUT buttons via GPIO open-drain / Hi-Z
//
// Connections:
//   OUT_A (GPIO4)  → BTN_A (GPIO32 DUT)
//   OUT_B (GPIO5)  → BTN_B (GPIO33 DUT)
//   OUT_C (GPIO6)  → BTN_C (GPIO25 DUT)
//   OUT_D (GPIO7)  → BTN_D (GPIO26 DUT)
//   GND            → GND DUT (common ground!)
//
// DUT: active LOW.
// Press  = pinMode(pin, OUTPUT) + digitalWrite(pin, LOW)
// Release= pinMode(pin, INPUT)  — Hi-Z, DUT pullup pulls to HIGH
//
// Serial protocol (115200):
//   PRESS A 2000\n        → press A for 2000ms, then release
//   PRESS_COMBO CD 2500\n → press C+D simultaneously for 2500ms
//   RELEASE ALL\n         → release all pins (Hi-Z)
//   PING\n                → responds PONG\n
//
// Responses: OK\n or ERR reason\n
// Safety: 10s without command → RELEASE ALL automatically

#include <Arduino.h>

#define OUT_A 4
#define OUT_B 5
#define OUT_C 6
#define OUT_D 7

#define SAFETY_TIMEOUT_MS 10000

static const int ALL_PINS[] = {OUT_A, OUT_B, OUT_C, OUT_D};
static const char ALL_NAMES[] = {'A', 'B', 'C', 'D'};
static const int PIN_COUNT = 4;

static unsigned long lastCmdMs = 0;

static int pinForName(char name)
{
    for (int i = 0; i < PIN_COUNT; i++)
        if (ALL_NAMES[i] == name) return ALL_PINS[i];
    return -1;
}

static void releaseAll()
{
    for (int i = 0; i < PIN_COUNT; i++)
        pinMode(ALL_PINS[i], INPUT);
}

static void pressPin(int pin)
{
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
}

static void handlePress(const char* args)
{
    // Format: "A 2000" or "B 500"
    char name = args[0];
    int pin = pinForName(name);
    if (pin < 0) {
        Serial.println("ERR unknown button");
        return;
    }
    int duration = atoi(args + 2);
    if (duration <= 0 || duration > 10000) {
        Serial.println("ERR bad duration");
        return;
    }

    pressPin(pin);
    Serial.println("OK");
    delay(duration);
    pinMode(pin, INPUT);
}

static void handlePressCombo(const char* args)
{
    // Format: "CD 2500" or "AB 1000"
    if (strlen(args) < 4) {
        Serial.println("ERR bad format");
        return;
    }
    char nameA = args[0];
    char nameB = args[1];
    int pinA = pinForName(nameA);
    int pinB = pinForName(nameB);
    if (pinA < 0 || pinB < 0) {
        Serial.println("ERR unknown button");
        return;
    }
    int duration = atoi(args + 3);
    if (duration <= 0 || duration > 10000) {
        Serial.println("ERR bad duration");
        return;
    }

    pressPin(pinA);
    pressPin(pinB);
    Serial.println("OK");
    delay(duration);
    pinMode(pinA, INPUT);
    pinMode(pinB, INPUT);
}

static void processCommand(const String& cmd)
{
    lastCmdMs = millis();

    if (cmd == "PING") {
        Serial.println("PONG");
    } else if (cmd == "RELEASE ALL") {
        releaseAll();
        Serial.println("OK");
    } else if (cmd.startsWith("PRESS_COMBO ")) {
        handlePressCombo(cmd.c_str() + 12);
    } else if (cmd.startsWith("PRESS ")) {
        handlePress(cmd.c_str() + 6);
    } else {
        Serial.print("ERR unknown command: ");
        Serial.println(cmd);
    }
}

void setup()
{
    Serial.begin(115200);
    releaseAll();
    lastCmdMs = millis();
    Serial.println("TESTER READY");
}

void loop()
{
    // Safety timeout: 10s without command → release everything
    if (millis() - lastCmdMs > SAFETY_TIMEOUT_MS) {
        releaseAll();
        lastCmdMs = millis();
    }

    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        if (line.length() > 0)
            processCommand(line);
    }
}
