#include "peripheral_power.h"

#include <Arduino.h>

#include "logging.h"
#include "zbox_config.h"

void peripheralPowerInitEarly()
{
    LOGI("[PWR] init switches off\n");
    // NS high-side switch OFF as hi-Z: pull-up R11 holds the gate at +3V3 (FET off).
    // Do NOT drive OUTPUT-HIGH here: pinMode(OUTPUT) latches the pin LOW for a few us
    // before digitalWrite(HIGH), and via R12 that briefly turns Q3 ON -> amp inrush ->
    // brownout reset loop at boot. hi-Z avoids the glitch entirely.
    pinMode(NS_EN, INPUT);
    LOGI("[PWR] NS_EN off\n");

    pinMode(NFC_EN, OUTPUT);
    digitalWrite(NFC_EN, LOW);
    LOGI("[PWR] NFC_EN off\n");
}

void nsPowerOn()
{
    LOGI("[PWR] NS on\n");
    // Set the latch LOW before enabling the driver so the pin never glitches HIGH.
    digitalWrite(NS_EN, LOW);
    pinMode(NS_EN, OUTPUT);
}

void nsPowerOff()
{
    LOGI("[PWR] NS off\n");
    // Release to hi-Z; R11 pulls the gate to +3V3 (FET off) with no output-low glitch.
    pinMode(NS_EN, INPUT);
}

void nfcPowerSwitchOn()
{
    LOGI("[PWR] NFC on\n");
    pinMode(NFC_EN, OUTPUT);
    digitalWrite(NFC_EN, HIGH);
}

void nfcPowerSwitchOff()
{
    LOGI("[PWR] NFC off\n");
    pinMode(NFC_EN, OUTPUT);
    digitalWrite(NFC_EN, LOW);
}

void nfcBusHiZForPowerOff()
{
    pinMode(PN532_SCK, INPUT);
    pinMode(PN532_MISO, INPUT);
    pinMode(PN532_MOSI, INPUT);
    pinMode(PN532_SS, INPUT);
}
