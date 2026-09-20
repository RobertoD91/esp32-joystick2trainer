// SPDX-License-Identifier: MIT
// Configurable mapping from a normalised joystick state to RC channels.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "rc/channels.hpp"
#include "rc/input_state.hpp"

namespace rc {

enum class SourceType : uint8_t {
    None = 0,  ///< Channel outputs its failsafe/constant value.
    Axis,      ///< An analogue axis (index = Axis).
    Button,    ///< A button (index = 0-based button number).
    HatX,      ///< Horizontal component of the hat switch.
    HatY,      ///< Vertical component of the hat switch.
    Constant,  ///< Always `failsafe_us`.
};

struct ChannelConfig {
    SourceType source = SourceType::None;
    uint8_t index = 0;
    bool invert = false;
    /// For buttons: toggle the output on each press instead of following the button.
    bool toggle = false;
    /// Dead band around the centre, in percent of half travel (0..50).
    uint8_t deadband_pct = 0;
    /// Exponential (0 = linear, 100 = strongest), applied after the dead band.
    uint8_t expo_pct = 0;
    /// Output end points.
    uint16_t min_us = kUsMin;
    uint16_t max_us = kUsMax;
    /// Offset added after scaling (sub-trim).
    int16_t trim_us = 0;
    /// Value emitted when the joystick is disconnected, when the source is None/Constant,
    /// or when a button toggle has never been pressed.
    uint16_t failsafe_us = kUsCenter;

    uint16_t centerUs() const { return static_cast<uint16_t>((min_us + max_us) / 2); }
};

struct MappingConfig {
    std::array<ChannelConfig, kMaxChannels> ch{};
};

/// Apply dead band and expo to a normalised value in [-1, 1].
float shapeAxis(float value, uint8_t deadband_pct, uint8_t expo_pct);

/// Stateful mapper (keeps the toggle state of buttons).
class Mapper {
public:
    /// Compute the channel frame for the given input.
    Frame map(const InputState& in, const MappingConfig& cfg);

    /// Frame to output while no joystick is connected.
    static Frame failsafeFrame(const MappingConfig& cfg);

    /// Reset toggle states (e.g. on joystick disconnect).
    void reset();

private:
    std::array<bool, kMaxChannels> toggle_state_{};
    std::array<bool, kMaxChannels> last_button_{};
};

const char* sourceTypeName(SourceType type);

}  // namespace rc
