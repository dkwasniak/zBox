#include "nfc_adapter.h"
#include "dispatcher.h"
#include "nfc_module.h"
#include "event_queue.h"
#include "events.h"
#include "logging.h"

void nfcAdapterDrain() {
    NfcEvent nfcEvt;
    while (nfcGetEvent(&nfcEvt, 0)) {
        if (nfcEvt.tagPresent) {
#if !DISPATCHER_OWNS_BT_NFC
            // Stage 1 dual-call: old handler owns playback policy
            playbackHandleNfcTagPresent(nfcEvt.uid);
#endif
            postEventFromTask(makeNfcDetectedEvent(nfcEvt.uid));
            LOGI("[NFC_ADAPTER] NfcTagDetected uid=%s\n", nfcEvt.uid);
        } else {
#if !DISPATCHER_OWNS_BT_NFC
            playbackHandleNfcTagRemoved();
#endif
            postEventFromTask(makeEvent(EventType::NfcTagRemoved));
            LOGI("[NFC_ADAPTER] NfcTagRemoved\n");
        }
    }
}
