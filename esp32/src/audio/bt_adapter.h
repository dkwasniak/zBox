#pragma once
#include "app_state.h"

// Call once after audioInit() to capture initial BT connection state.
void btAdapterInit();

// Call each loop iteration. Runs old audioPollBtConnection() (dual-call)
// then posts BtConnected / BtDisconnected to dispatcher on state change.
void btAdapterPoll();

// Stage 2+: called by dispatcher executor when DISPATCHER_OWNS_BT_NFC = 1
void btAdapterTriggerRecoveryPulse(CmdId cmd_id);
void btAdapterTriggerDiscoveryRestart(CmdId cmd_id);
void btAdapterShutdown(CmdId cmd_id);
