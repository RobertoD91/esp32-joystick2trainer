// SPDX-License-Identifier: MIT
// Core RC channel definitions shared by every layer of the firmware.
// This header is hardware independent and compiles on the host for unit tests.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rc {

/// Maximum number of RC channels carried by any output protocol (SBUS = 16).
constexpr std::size_t kMaxChannels = 16;

/// Nominal servo pulse limits in microseconds.
constexpr uint16_t kUsMin = 1000;
constexpr uint16_t kUsCenter = 1500;
constexpr uint16_t kUsMax = 2000;

/// Absolute limits accepted anywhere in the pipeline (SBUS 172..1811 maps to 988..2012 us).
constexpr uint16_t kUsAbsoluteMin = 800;
constexpr uint16_t kUsAbsoluteMax = 2200;

/// A frame of RC channel values, expressed as servo pulse widths in microseconds.
struct Frame {
    std::array<uint16_t, kMaxChannels> us{};
    /// True when the values are failsafe values (no valid joystick input available).
    bool failsafe = false;

    static Frame centered() {
        Frame f;
        f.us.fill(kUsCenter);
        return f;
    }
};

/// Clamp a pulse width to the absolute limits.
constexpr uint16_t clampUs(int32_t value) {
    if (value < kUsAbsoluteMin) {
        return kUsAbsoluteMin;
    }
    if (value > kUsAbsoluteMax) {
        return kUsAbsoluteMax;
    }
    return static_cast<uint16_t>(value);
}

}  // namespace rc
