#pragma once
#include "app_state.h"

// Call once after audioInit() to capture initial BT connection state.
void btAdapterInit();

// Call each loop iteration and post BtConnected/BtDisconnected edges while
// the temporary headphones mode is active.
void btAdapterPoll();

void btAdapterStartHeadphonesMode(CmdId cmd_id);
void btAdapterStopHeadphonesMode(CmdId cmd_id);
