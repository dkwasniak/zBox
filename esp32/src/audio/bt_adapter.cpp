#include "bt_adapter.h"
#include "dispatcher.h"
#include "audio.h"
#include "state.h"
#include "event_queue.h"
#include "events.h"
#include "logging.h"
#include <esp_bt.h>
#include <Arduino.h>

static bool s_prevConnected = false;

void btAdapterInit() {
    s_prevConnected = audioBtIsConnected();
}

void btAdapterPoll() {
#if DISPATCHER_OWNS_BT_NFC
    // Dispatcher authoritative: poll directly; skip old audioPollBtConnection().
    // Write g_btConnected for audio.cpp internal state tracking.
    bool connected = audioBtIsConnected();
    g_btConnected = connected;
#else
    // Stage 1 dual-call: run old handler first so g_btConnected and loop() logic work.
    audioPollBtConnection();
    bool connected = audioBtIsConnected();
#endif

    if (connected != s_prevConnected) {
        s_prevConnected = connected;
        EventType type = connected ? EventType::BtConnected : EventType::BtDisconnected;
        postEventFromTask(makeEvent(type));
        LOGI("[BT_ADAPTER] %s\n", connected ? "BtConnected" : "BtDisconnected");
    }
}

void btAdapterTriggerRecoveryPulse(CmdId cmd_id) {
    (void)cmd_id;  // Stage 2+
}

void btAdapterTriggerDiscoveryRestart(CmdId cmd_id) {
    (void)cmd_id;  // Stage 2+
}

void btAdapterShutdown(CmdId cmd_id) {
    (void)cmd_id;
    LOGC("[BT_ADAPTER] Shutting down BT controller\n");
    esp_err_t err = esp_bt_controller_disable();
    if (err != ESP_OK) {
        LOGW("[BT_ADAPTER] esp_bt_controller_disable err=%d (ok if already disabled)\n", (int)err);
    }
    delay(50);
    // Always report success — sleep must not be blocked by BT state
    postEventFromTask(makeEvent(EventType::BtShutdownCompleted));
}
