// SPDX-License-Identifier: MIT
// Normalised joystick input, the boundary between the USB/HID layer and the RC mapping.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rc {

/// Logical axes recognised by the mapper. The HID layer fills them from the
/// Generic Desktop / Simulation Controls usages; unsupported axes stay at 0.
enum class Axis : uint8_t { X = 0, Y, Z, Rx, Ry, Rz, Slider, Dial, Wheel, Throttle, Rudder, Accelerator, Brake, Count };

constexpr std::size_t kAxisCount = static_cast<std::size_t>(Axis::Count);
constexpr std::size_t kMaxButtons = 32;

/// Short lower-case names used by the configuration CLI ("x", "y", "rz", "slider", ...).
const char* axisName(Axis axis);
/// Parse an axis name; returns false when unknown.
bool axisFromName(const char* name, Axis& out);

struct InputState {
    /// Normalised axis positions in [-1.0, +1.0]. 0 is centre.
    std::array<float, kAxisCount> axis{};
    /// Which axes were actually reported by the device (others are always 0).
    std::array<bool, kAxisCount> axis_present{};
    /// Button bitmask, bit 0 = button 1.
    uint32_t buttons = 0;
    /// Number of buttons reported by the device.
    uint8_t button_count = 0;
    /// Hat switch direction: -1 = released/centre, 0 = up, 1 = up-right, ... 7 = up-left.
    int8_t hat = -1;
    bool hat_present = false;

    bool button(std::size_t index) const { return index < kMaxButtons && ((buttons >> index) & 1u) != 0; }

    /// Hat decomposed into a horizontal component: -1 (left), 0, +1 (right).
    int8_t hatX() const;
    /// Hat decomposed into a vertical component: -1 (down), 0, +1 (up).
    int8_t hatY() const;
};

}  // namespace rc
