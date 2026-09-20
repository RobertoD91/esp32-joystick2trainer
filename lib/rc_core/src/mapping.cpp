// SPDX-License-Identifier: MIT
#include "rc/mapping.hpp"

#include <cmath>

namespace rc {

namespace {

float clamp1(float v) {
    if (v < -1.0f) {
        return -1.0f;
    }
    if (v > 1.0f) {
        return 1.0f;
    }
    return v;
}

uint16_t scale(float v, const ChannelConfig& c) {
    v = clamp1(c.invert ? -v : v);
    const float half = (static_cast<float>(c.max_us) - static_cast<float>(c.min_us)) / 2.0f;
    const float center = static_cast<float>(c.min_us) + half;
    int32_t us = static_cast<int32_t>(std::lround(center + v * half)) + c.trim_us;
    if (us < c.min_us) {
        us = c.min_us;
    }
    if (us > c.max_us) {
        us = c.max_us;
    }
    return clampUs(us);
}

}  // namespace

float shapeAxis(float value, uint8_t deadband_pct, uint8_t expo_pct) {
    value = clamp1(value);
    if (deadband_pct > 50) {
        deadband_pct = 50;
    }
    if (expo_pct > 100) {
        expo_pct = 100;
    }
    const float db = static_cast<float>(deadband_pct) / 100.0f;
    float mag = std::fabs(value);
    if (db > 0.0f) {
        if (mag <= db) {
            return 0.0f;
        }
        mag = (mag - db) / (1.0f - db);  // rescale so full travel is still reachable
    }
    if (expo_pct > 0) {
        const float e = static_cast<float>(expo_pct) / 100.0f;
        mag = (1.0f - e) * mag + e * mag * mag * mag;
    }
    return value < 0.0f ? -mag : mag;
}

Frame Mapper::map(const InputState& in, const MappingConfig& cfg) {
    Frame out;
    out.failsafe = false;
    for (std::size_t i = 0; i < kMaxChannels; ++i) {
        const ChannelConfig& c = cfg.ch[i];
        switch (c.source) {
            case SourceType::Axis: {
                auto idx = static_cast<std::size_t>(c.index);
                float v = idx < kAxisCount ? in.axis[idx] : 0.0f;
                out.us[i] = scale(shapeAxis(v, c.deadband_pct, c.expo_pct), c);
                break;
            }
            case SourceType::Button: {
                const bool pressed = in.button(c.index);
                bool active = pressed;
                if (c.toggle) {
                    if (pressed && !last_button_[i]) {
                        toggle_state_[i] = !toggle_state_[i];
                    }
                    active = toggle_state_[i];
                }
                last_button_[i] = pressed;
                out.us[i] = scale(active ? 1.0f : -1.0f, c);
                break;
            }
            case SourceType::HatX:
                out.us[i] = scale(static_cast<float>(in.hatX()), c);
                break;
            case SourceType::HatY:
                out.us[i] = scale(static_cast<float>(in.hatY()), c);
                break;
            case SourceType::Constant:
            case SourceType::None:
            default:
                out.us[i] = clampUs(c.failsafe_us);
                break;
        }
    }
    return out;
}

Frame Mapper::failsafeFrame(const MappingConfig& cfg) {
    Frame out;
    out.failsafe = true;
    for (std::size_t i = 0; i < kMaxChannels; ++i) {
        out.us[i] = clampUs(cfg.ch[i].failsafe_us);
    }
    return out;
}

void Mapper::reset() {
    toggle_state_.fill(false);
    last_button_.fill(false);
}

const char* sourceTypeName(SourceType type) {
    switch (type) {
        case SourceType::None:
            return "none";
        case SourceType::Axis:
            return "axis";
        case SourceType::Button:
            return "btn";
        case SourceType::HatX:
            return "hatx";
        case SourceType::HatY:
            return "haty";
        case SourceType::Constant:
            return "const";
    }
    return "?";
}

}  // namespace rc
