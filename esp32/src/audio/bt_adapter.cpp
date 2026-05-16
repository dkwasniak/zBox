#include "bt_adapter.h"
#include "dispatcher.h"
#include "audio.h"
#include "state.h"
#include "event_queue.h"
#include "events.h"
#include "logging.h"

static bool s_prevConnected = false;
static bool s_stopPending = false;

static void postBtStopCompleted() {
    s_stopPending = false;
    s_prevConnected = false;
    postEventFromTask(makeEvent(EventType::BtHeadphonesModeStopped));
    LOGI("[BT_ADAPTER] BtHeadphonesModeStopped\n");
}

void btAdapterInit() {
    s_prevConnected = audioBtHeadphonesAreConnected();
    s_stopPending = false;
}

void btAdapterPoll() {
    const bool running = audioBtHeadphonesModeIsRunning();
    const bool connected = running && audioBtHeadphonesAreConnected();

    if (s_stopPending) {
        if (!running) {
            postBtStopCompleted();
        }
        return;
    }

    if (connected != s_prevConnected) {
        s_prevConnected = connected;
        postEventFromTask(makeEvent(connected ? EventType::BtConnected : EventType::BtDisconnected));
        LOGI("[BT_ADAPTER] %s\n", connected ? "BtConnected" : "BtDisconnected");
    }
}

void btAdapterStartHeadphonesMode(CmdId cmd_id) {
    (void)cmd_id;
    s_stopPending = false;
    if (!audioStartBtHeadphonesMode()) {
        postEventFromTask(makeEvent(EventType::BtHeadphonesModeStartFailed));
        return;
    }
    const bool connected = audioBtHeadphonesAreConnected();
    s_prevConnected = connected;
    if (connected) {
        postEventFromTask(makeEvent(EventType::BtConnected));
        LOGI("[BT_ADAPTER] BtConnected (already connected on mode start)\n");
    }
}

void btAdapterStopHeadphonesMode(CmdId cmd_id) {
    (void)cmd_id;
    s_stopPending = true;
    audioStopBtHeadphonesMode();
    if (!audioBtHeadphonesModeIsRunning()) {
        postBtStopCompleted();
    }
}
