// SPDX-License-Identifier: MIT
// Turns raw HID input reports into a normalised rc::InputState using the parsed
// report descriptor. Works with any joystick/gamepad that follows the HID usage tables.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "hid/report_descriptor.hpp"
#include "rc/input_state.hpp"

namespace hid {

class JoystickDecoder {
public:
    /// Build the decode tables from a parsed descriptor. Returns false when the descriptor
    /// contains nothing a joystick mapper can use (no axes, buttons or hat).
    bool configure(const ParsedDescriptor& desc);

    /// Decode a raw input report (as received from the interrupt IN endpoint, including the
    /// report ID byte when the device uses report IDs). Fields belonging to other report IDs
    /// are left untouched in `state`. Returns false when the report is not an input report
    /// we know about.
    bool decode(const uint8_t* report, std::size_t len, rc::InputState& state) const;

    /// Introspection for the CLI.
    struct AxisBinding {
        bool present = false;
        InputField field{};
    };
    const AxisBinding& axisBinding(rc::Axis axis) const { return axes_[static_cast<std::size_t>(axis)]; }
    const AxisBinding& hatBinding() const { return hat_; }
    std::size_t buttonCount() const { return button_count_; }
    bool usesReportIds() const { return uses_report_ids_; }

    /// Extract an unsigned/signed field from a payload. Public for testing.
    static int32_t extract(const uint8_t* payload, std::size_t payload_len, const InputField& f, std::size_t element,
                           bool& ok);

private:
    struct ButtonBinding {
        InputField field{};
        uint8_t button_index = 0;  ///< 0-based index into InputState::buttons
    };

    std::array<AxisBinding, rc::kAxisCount> axes_{};
    AxisBinding hat_{};
    std::array<ButtonBinding, rc::kMaxButtons> buttons_{};
    std::size_t button_binding_count_ = 0;
    std::array<InputField, 8> button_arrays_{};
    std::size_t button_array_count_ = 0;
    std::size_t button_count_ = 0;
    bool uses_report_ids_ = false;
};

/// Map a (usage page, usage) pair to a logical axis. Returns false when it is not an axis.
bool axisFromUsage(uint16_t usage_page, uint16_t usage, rc::Axis& out);

}  // namespace hid
