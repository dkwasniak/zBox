#pragma once
#include <Arduino.h>
#include "shared_types.h"

bool nfcInit();
bool nfcIsReady();
bool nfcPrescan(char *uidBuf, size_t len);
void nfcStartTask();
bool nfcGetEvent(NfcEvent *e, TickType_t timeout);
void nfcStopTaskForSleep();
void nfcPrepareForPowerOff();
uint32_t nfcGetTaskHWM();
