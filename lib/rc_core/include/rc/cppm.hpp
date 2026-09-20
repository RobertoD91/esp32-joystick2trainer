// SPDX-License-Identifier: MIT
// CPPM (PPM sum) frame encoder.
// Produces a list of (level, duration) segments that a hardware pulse generator
// (the ESP32 RMT peripheral in this project) plays back. No timing loops here.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "rc/channels.hpp"

namespace rc::cppm {

constexpr uint8_t kMinChannels = 1;
constexpr uint8_t kMaxChannels = 16;

struct Config {
    /// Number of channels encoded in each frame (typical: 8).
    uint8_t channels = 8;
    /// Fixed width of the marker pulse that starts every channel slot (typical: 300 us).
    uint16_t pulse_us = 300;
    /// Nominal frame period. If the channels do not fit while keeping `min_sync_us`,
    /// the frame is stretched (standard behaviour of most encoders).
    uint32_t frame_us = 22500;
    /// Synchronisation gap limits (from the sync pulse start to the end of the frame).
    /// EdgeTX accepts a gap between 4 and 19 ms; anything longer is NOT treated as sync,
    /// so the frame is shortened when few channels are used.
    uint32_t min_sync_us = 4000;
    uint32_t max_sync_us = 18000;
    /// Channel value limits enforced before encoding.
    uint16_t min_us = 900;
    uint16_t max_us = 2100;
    /// false: idle HIGH, pulses LOW (standard "negative" PPM used by most trainers).
    /// true:  idle LOW, pulses HIGH ("positive" PPM).
    bool inverted = false;
};

/// One constant-level segment of the output waveform.
struct Segment {
    bool level;
    uint32_t us;
};

/// A frame is at most (channels + 1) pulses, each made of a marker and a space: 2 * 17 segments.
constexpr std::size_t kMaxSegments = 2 * (kMaxChannels + 1);

struct Waveform {
    std::array<Segment, kMaxSegments> segments{};
    std::size_t count = 0;
    /// Total duration of the waveform in microseconds (equals the actual frame period).
    uint32_t total_us = 0;
    /// The level the line rests at between frames (the idle level).
    bool idle_level = true;
};

/// Validate and normalise a configuration (clamps channels/pulse/frame to sane values).
Config sanitize(const Config& cfg);

/// Encode one frame. Channel `i` of the frame maps to CPPM slot `i`.
/// Channels beyond `cfg.channels` are ignored.
Waveform encode(const Frame& frame, const Config& cfg);

}  // namespace rc::cppm
