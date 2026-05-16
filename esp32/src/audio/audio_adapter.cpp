#include "audio_adapter.h"
#include "audio.h"
#include "dispatcher.h"
#include "event_queue.h"
#include "events.h"
#include "effects.h"
#include "state.h"
#include "playback.h"
#include "sd_storage.h"
#include "logging.h"
#include <Arduino.h>

void audioAdapterInit() {
    // Stage 1 stub; no runtime setup needed.
}

#if DISPATCHER_OWNS_AUDIO

bool audioAdapterStartNfcPlayback(const char *uid, CmdId cmd_id)
{
    if (!uid || !*uid) {
        postEventFromTask(makeNfcPlaybackStartFailedEvent(
            uid ? uid : "", PlaybackFailReason::MappingNotFound, cmd_id));
        return false;
    }

    char path[256] = {};
    if (!lookupMappingPath(uid, path, sizeof(path))) {
        LOGW("[AUDIO_ADAP] No mapping for uid=%s cmd_id=%u\n", uid, (unsigned)cmd_id);
        postEventFromTask(makeNfcPlaybackStartFailedEvent(
            uid, PlaybackFailReason::MappingNotFound, cmd_id));
        return false;
    }

    if (!audioStartNfcTrack(path, uid, cmd_id)) {
        LOGW("[AUDIO_ADAP] Queue full, NFC start rejected cmd_id=%u\n", (unsigned)cmd_id);
        postEventFromTask(makeAudioCommandRejectedEvent(
            cmd_id, PlaybackFailReason::AudioCommandRejected));
        return false;
    }
    return true;
}

bool audioAdapterStartMusicTrack(uint16_t index, CmdId cmd_id)
{
    char path[256] = {};
    if (!playbackGetMusicPath(index, path, sizeof(path))) {
        LOGW("[AUDIO_ADAP] Music index %u out of range cmd_id=%u\n",
             (unsigned)index, (unsigned)cmd_id);
        postEventFromTask(makeMusicTrackStartFailedEvent(
            index, PlaybackFailReason::MappingNotFound, cmd_id));
        return false;
    }

    if (!audioStartMusicTrack(path, index, cmd_id)) {
        LOGW("[AUDIO_ADAP] Queue full, music start rejected cmd_id=%u\n", (unsigned)cmd_id);
        postEventFromTask(makeAudioCommandRejectedEvent(
            cmd_id, PlaybackFailReason::AudioCommandRejected));
        return false;
    }
    return true;
}

void audioAdapterStop(CmdId cmd_id)
{
    if (!audioStopWithId(cmd_id)) {
        // Stop queue is front-priority; full is extremely rare. Post AudioStopped best-effort.
        LOGW("[AUDIO_ADAP] Stop queue full, best-effort AudioStopped cmd_id=%u\n", (unsigned)cmd_id);
        postEventFromTask(makeAudioStoppedEvent(cmd_id));
    }
}

void audioAdapterPause(CmdId cmd_id)
{
    (void)cmd_id;
    audioPause();  // Stage 5 will add feedback events for pause/resume.
}

void audioAdapterResume(CmdId cmd_id)
{
    (void)cmd_id;
    audioResume();
}

bool audioAdapterPlaySystemSound(uint8_t sound_id, CmdId cmd_id)
{
    const char *name = nullptr;
    switch (sound_id) {
        case SOUND_ID_POWER_OFF:  name = "power_off";  break;
        case SOUND_ID_NFC_MODE:   name = "nfc_mode";   break;
        case SOUND_ID_MUSIC_MODE: name = "music_mode"; break;
        case SOUND_ID_STARTUP:    name = "startup";    break;
        default: break;
    }

    if (!name) {
        LOGW("[AUDIO_ADAP] Unknown sound_id=%u cmd_id=%u\n", (unsigned)sound_id, (unsigned)cmd_id);
        postEventFromTask(makeSystemSoundFailedEvent(
            sound_id, SoundFailReason::FileNotFound, cmd_id));
        return false;
    }

    char path[256] = {};
    if (!lookupSystemSoundPath(name, path, sizeof(path))) {
        LOGW("[AUDIO_ADAP] System sound '%s' not in map cmd_id=%u\n", name, (unsigned)cmd_id);
        postEventFromTask(makeSystemSoundFailedEvent(
            sound_id, SoundFailReason::FileNotFound, cmd_id));
        return false;
    }

    if (!audioPlaySystemSound(path, sound_id, cmd_id)) {
        LOGW("[AUDIO_ADAP] Queue full, system sound rejected cmd_id=%u\n", (unsigned)cmd_id);
        postEventFromTask(makeAudioCommandRejectedEvent(
            cmd_id, PlaybackFailReason::AudioCommandRejected));
        return false;
    }
    return true;
}

#endif // DISPATCHER_OWNS_AUDIO
