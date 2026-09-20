// SPDX-License-Identifier: MIT
#include "hid/joystick_decoder.hpp"

namespace hid {

bool axisFromUsage(uint16_t usage_page, uint16_t usage, rc::Axis& out) {
    if (usage_page == kPageGenericDesktop) {
        switch (usage) {
            case kUsageX:
                out = rc::Axis::X;
                return true;
            case kUsageY:
                out = rc::Axis::Y;
                return true;
            case kUsageZ:
                out = rc::Axis::Z;
                return true;
            case kUsageRx:
                out = rc::Axis::Rx;
                return true;
            case kUsageRy:
                out = rc::Axis::Ry;
                return true;
            case kUsageRz:
                out = rc::Axis::Rz;
                return true;
            case kUsageSlider:
                out = rc::Axis::Slider;
                return true;
            case kUsageDial:
                out = rc::Axis::Dial;
                return true;
            case kUsageWheel:
                out = rc::Axis::Wheel;
                return true;
            default:
                return false;
        }
    }
    if (usage_page == kPageSimulation) {
        switch (usage) {
            case kUsageSimThrottle:
                out = rc::Axis::Throttle;
                return true;
            case kUsageSimRudder:
                out = rc::Axis::Rudder;
                return true;
            case kUsageSimAccelerator:
                out = rc::Axis::Accelerator;
                return true;
            case kUsageSimBrake:
                out = rc::Axis::Brake;
                return true;
            default:
                return false;
        }
    }
    return false;
}

bool JoystickDecoder::configure(const ParsedDescriptor& desc) {
    *this = JoystickDecoder{};
    uses_report_ids_ = desc.uses_report_ids;
    uint32_t max_button = 0;

    for (std::size_t i = 0; i < desc.field_count; ++i) {
        const InputField& f = desc.fields[i];
        if (f.is_relative || f.bit_size == 0 || f.bit_size > 32) {
            continue;
        }
        if (f.usage_page == kPageButton) {
            if (f.is_array) {
                if (button_array_count_ < button_arrays_.size()) {
                    button_arrays_[button_array_count_++] = f;
                    if (f.usage_max > max_button) {
                        max_button = f.usage_max;
                    }
                }
            } else if (f.usage >= 1 && f.usage <= rc::kMaxButtons && button_binding_count_ < rc::kMaxButtons) {
                buttons_[button_binding_count_++] = ButtonBinding{f, static_cast<uint8_t>(f.usage - 1)};
                if (f.usage > max_button) {
                    max_button = f.usage;
                }
            }
            continue;
        }
        if (f.is_array) {
            continue;
        }
        if (f.usage_page == kPageGenericDesktop && f.usage == kUsageHatSwitch) {
            if (!hat_.present) {
                hat_ = AxisBinding{true, f};
            }
            continue;
        }
        rc::Axis axis;
        if (!axisFromUsage(f.usage_page, f.usage, axis)) {
            continue;
        }
        AxisBinding& slot = axes_[static_cast<std::size_t>(axis)];
        if (slot.present) {
            // Common quirk: two "Slider" usages (throttle + extra lever). Keep the second one
            // reachable by aliasing it to Dial if that is free.
            if (axis == rc::Axis::Slider && !axes_[static_cast<std::size_t>(rc::Axis::Dial)].present) {
                axes_[static_cast<std::size_t>(rc::Axis::Dial)] = AxisBinding{true, f};
            }
            continue;
        }
        slot = AxisBinding{true, f};
    }

    button_count_ = max_button > rc::kMaxButtons ? rc::kMaxButtons : max_button;
    bool any_axis = false;
    for (const auto& a : axes_) {
        any_axis = any_axis || a.present;
    }
    return any_axis || hat_.present || button_count_ > 0;
}

int32_t JoystickDecoder::extract(const uint8_t* payload, std::size_t payload_len, const InputField& f,
                                 std::size_t element, bool& ok) {
    const uint32_t start_bit = f.bit_offset + static_cast<uint32_t>(element) * f.bit_size;
    const uint32_t end_bit = start_bit + f.bit_size;
    if (f.bit_size == 0 || f.bit_size > 32 || end_bit > payload_len * 8) {
        ok = false;
        return 0;
    }
    ok = true;
    uint32_t value = 0;
    for (uint32_t b = 0; b < f.bit_size; ++b) {
        const uint32_t bit = start_bit + b;
        if (payload[bit >> 3] & (1u << (bit & 7))) {
            value |= (1u << b);
        }
    }
    if (f.logical_min < 0 && f.bit_size < 32) {
        // Sign-extend two's complement values.
        const uint32_t sign = 1u << (f.bit_size - 1);
        if (value & sign) {
            value |= ~((sign << 1) - 1);
        }
    }
    return static_cast<int32_t>(value);
}

namespace {

float normalise(int32_t v, const InputField& f) {
    const int32_t span = f.logical_max - f.logical_min;
    if (span <= 0) {
        return 0.0f;
    }
    if (v < f.logical_min) {
        v = f.logical_min;
    }
    if (v > f.logical_max) {
        v = f.logical_max;
    }
    if (f.logical_min < 0 && f.logical_max > 0) {
        // Signed range (e.g. -512..511): the device defines 0 as the centre, so scale each
        // side independently to keep the centre exact.
        return v < 0 ? static_cast<float>(v) / static_cast<float>(-f.logical_min)
                     : static_cast<float>(v) / static_cast<float>(f.logical_max);
    }
    return (static_cast<float>(v - f.logical_min) / static_cast<float>(span)) * 2.0f - 1.0f;
}

int8_t hatDirection(int32_t v, const InputField& f) {
    const int32_t span = f.logical_max - f.logical_min + 1;
    if (v < f.logical_min || v > f.logical_max) {
        return -1;  // null state
    }
    const int32_t rel = v - f.logical_min;
    if (span == 8) {
        return static_cast<int8_t>(rel);
    }
    if (span == 4) {
        return static_cast<int8_t>(rel * 2);
    }
    return -1;
}

}  // namespace

bool JoystickDecoder::decode(const uint8_t* report, std::size_t len, rc::InputState& state) const {
    if (report == nullptr || len == 0) {
        return false;
    }
    uint8_t report_id = 0;
    const uint8_t* payload = report;
    std::size_t payload_len = len;
    if (uses_report_ids_) {
        report_id = report[0];
        payload = report + 1;
        payload_len = len - 1;
    }

    bool decoded_any = false;
    bool ok = false;

    for (std::size_t i = 0; i < rc::kAxisCount; ++i) {
        const AxisBinding& a = axes_[i];
        if (!a.present || a.field.report_id != report_id) {
            continue;
        }
        const int32_t v = extract(payload, payload_len, a.field, 0, ok);
        if (ok) {
            state.axis[i] = normalise(v, a.field);
            state.axis_present[i] = true;
            decoded_any = true;
        }
    }

    if (hat_.present && hat_.field.report_id == report_id) {
        const int32_t v = extract(payload, payload_len, hat_.field, 0, ok);
        if (ok) {
            state.hat = hatDirection(v, hat_.field);
            state.hat_present = true;
            decoded_any = true;
        }
    }

    bool touched_buttons = false;
    uint32_t buttons = state.buttons;
    for (std::size_t i = 0; i < button_binding_count_; ++i) {
        const ButtonBinding& b = buttons_[i];
        if (b.field.report_id != report_id) {
            continue;
        }
        const int32_t v = extract(payload, payload_len, b.field, 0, ok);
        if (!ok) {
            continue;
        }
        touched_buttons = true;
        const uint32_t mask = 1u << b.button_index;
        if (v != 0) {
            buttons |= mask;
        } else {
            buttons &= ~mask;
        }
    }
    for (std::size_t i = 0; i < button_array_count_; ++i) {
        const InputField& f = button_arrays_[i];
        if (f.report_id != report_id) {
            continue;
        }
        // Clear every button of the array's range, then set the reported ones.
        for (uint32_t u = f.usage; u <= f.usage_max && u <= rc::kMaxButtons; ++u) {
            if (u >= 1) {
                buttons &= ~(1u << (u - 1));
            }
        }
        for (std::size_t e = 0; e < f.count; ++e) {
            const int32_t v = extract(payload, payload_len, f, e, ok);
            if (!ok) {
                break;
            }
            touched_buttons = true;
            if (v < f.logical_min || v > f.logical_max) {
                continue;
            }
            const int32_t usage = f.usage + (v - f.logical_min);
            if (usage >= 1 && usage <= static_cast<int32_t>(rc::kMaxButtons) && usage <= f.usage_max) {
                buttons |= 1u << (usage - 1);
            }
        }
    }
    if (touched_buttons) {
        state.buttons = buttons;
        decoded_any = true;
    }
    state.button_count = static_cast<uint8_t>(button_count_);
    return decoded_any;
}

}  // namespace hid
