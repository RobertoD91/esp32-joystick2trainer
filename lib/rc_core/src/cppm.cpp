// SPDX-License-Identifier: MIT
#include "rc/cppm.hpp"

namespace rc::cppm {

Config sanitize(const Config& in) {
    Config cfg = in;
    if (cfg.channels < kMinChannels) {
        cfg.channels = kMinChannels;
    }
    if (cfg.channels > kMaxChannels) {
        cfg.channels = kMaxChannels;
    }
    if (cfg.pulse_us < 100) {
        cfg.pulse_us = 100;
    }
    if (cfg.pulse_us > 600) {
        cfg.pulse_us = 600;
    }
    if (cfg.min_us < kUsAbsoluteMin) {
        cfg.min_us = kUsAbsoluteMin;
    }
    if (cfg.max_us > kUsAbsoluteMax) {
        cfg.max_us = kUsAbsoluteMax;
    }
    if (cfg.min_us + 2 * cfg.pulse_us > cfg.max_us) {
        cfg.min_us = kUsAbsoluteMin;
        cfg.max_us = kUsAbsoluteMax;
    }
    if (cfg.min_sync_us < 2000) {
        cfg.min_sync_us = 2000;
    }
    if (cfg.max_sync_us < cfg.min_sync_us + 1000) {
        cfg.max_sync_us = cfg.min_sync_us + 1000;
    }
    if (cfg.frame_us < 5000) {
        cfg.frame_us = 5000;
    }
    if (cfg.frame_us > 60000) {
        cfg.frame_us = 60000;
    }
    return cfg;
}

Waveform encode(const Frame& frame, const Config& in) {
    const Config cfg = sanitize(in);
    const bool pulse_level = cfg.inverted;  // pulses are LOW in standard (non-inverted) PPM
    const bool space_level = !cfg.inverted;

    Waveform wf;
    wf.idle_level = space_level;

    uint32_t used_us = 0;
    for (uint8_t ch = 0; ch < cfg.channels; ++ch) {
        uint32_t value = frame.us[ch];
        if (value < cfg.min_us) {
            value = cfg.min_us;
        }
        if (value > cfg.max_us) {
            value = cfg.max_us;
        }
        wf.segments[wf.count++] = Segment{pulse_level, cfg.pulse_us};
        wf.segments[wf.count++] = Segment{space_level, value - cfg.pulse_us};
        used_us += value;
    }

    // Synchronisation slot: marker pulse followed by the remaining gap.
    uint32_t sync_us = (cfg.frame_us > used_us) ? (cfg.frame_us - used_us) : 0;
    if (sync_us < cfg.min_sync_us) {
        sync_us = cfg.min_sync_us;  // stretch the frame rather than break sync detection
    }
    if (sync_us > cfg.max_sync_us) {
        sync_us = cfg.max_sync_us;  // shorten the frame: a too long gap is not a valid sync
    }
    wf.segments[wf.count++] = Segment{pulse_level, cfg.pulse_us};
    wf.segments[wf.count++] = Segment{space_level, sync_us - cfg.pulse_us};
    wf.total_us = used_us + sync_us;
    return wf;
}

}  // namespace rc::cppm
