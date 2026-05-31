#include "led_scene.h"

LedSceneParams deriveLedScene(const AppState& s) {
    LedSceneParams p{};

    // Priority 1: any sleep-preparation state
    if (s.sleep_state != SleepState::Awake) {
        p.type = LedSceneType::SleepReady;
        return p;
    }

    // Priority 2: sync mode entry
    if (s.sync_mode) {
        p.type = LedSceneType::SyncEntry;
        return p;
    }

    // Priority 4: night-light session
    if (s.session_mode == SessionMode::NightLight) {
        p.type = LedSceneType::NightLight;
        p.params.night_light.percent = s.night_light_brightness_percent;
        return p;
    }

    // Priority 5: boot not yet complete
    if (s.boot_state != BootState::Ready) {
        p.type = LedSceneType::BootProgress;
        return p;
    }

    // Priority 6: waiting for BT speaker
    if (s.bt_headphones_mode_active &&
        s.bt_headphones_state == BtHeadphonesState::WaitingForHeadphones) {
        p.type = LedSceneType::WaitBt;
        return p;
    }

    // Priority 7: battery preview overlay
    if (s.battery_preview_active) {
        p.type = LedSceneType::BatteryPreview;
        p.params.battery.bars = s.battery_bars;
        return p;
    }

    // Priority 8: volume overlay (active while deadline is non-zero)
    if (s.volume_overlay_deadline_ms != 0) {
        p.type = LedSceneType::VolumeOverlay;
        p.params.volume.level = s.volume_overlay_level;
        return p;
    }

    // Priority 9: playing (local speaker only — BT headphones mode shows Idle)
    if ((s.audio_state == AudioState::PlayingFile ||
         s.audio_state == AudioState::StartingFile) &&
        s.output_mode != AudioOutputMode::BtHeadphones) {
        p.type = LedSceneType::Playing;
        return p;
    }

    // Priority 10 + default: idle
    p.type = LedSceneType::Idle;
    return p;
}
