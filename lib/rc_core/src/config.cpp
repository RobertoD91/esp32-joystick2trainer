// SPDX-License-Identifier: MIT
#include "rc/config.hpp"

#include <cstring>

namespace rc {

namespace {

ChannelConfig axisChannel(Axis axis, bool invert = false) {
    ChannelConfig c;
    c.source = SourceType::Axis;
    c.index = static_cast<uint8_t>(axis);
    c.invert = invert;
    c.deadband_pct = 2;
    return c;
}

ChannelConfig buttonChannel(uint8_t button_index, bool toggle = false) {
    ChannelConfig c;
    c.source = SourceType::Button;
    c.index = button_index;
    c.toggle = toggle;
    c.failsafe_us = kUsMin;
    return c;
}

ChannelConfig hatChannel(SourceType type) {
    ChannelConfig c;
    c.source = type;
    return c;
}

}  // namespace

Config defaultConfig() {
    Config cfg;
    cfg.cppm.wave.channels = J2T_CPPM_CHANNELS;
    cfg.cppm.wave.frame_us = J2T_CPPM_FRAME_US;

    // AETR-style default: aileron, elevator, throttle, rudder, then buttons and hat.
    // HID Y grows when the stick is pulled back, RC elevator grows when pushed forward: invert.
    // The SideWinder throttle lever reports 0 when pushed fully forward: invert as well.
    cfg.mapping.ch[0] = axisChannel(Axis::X);
    cfg.mapping.ch[1] = axisChannel(Axis::Y, true);
    cfg.mapping.ch[2] = axisChannel(Axis::Slider, true);
    cfg.mapping.ch[2].failsafe_us = kUsMin;  // throttle low when the stick is gone
    cfg.mapping.ch[3] = axisChannel(Axis::Rz);
    cfg.mapping.ch[4] = buttonChannel(0);  // trigger
    cfg.mapping.ch[5] = buttonChannel(1);  // thumb button
    cfg.mapping.ch[6] = hatChannel(SourceType::HatX);
    cfg.mapping.ch[7] = hatChannel(SourceType::HatY);
    cfg.mapping.ch[8] = buttonChannel(2);
    cfg.mapping.ch[9] = buttonChannel(3);
    cfg.mapping.ch[10] = buttonChannel(4);
    cfg.mapping.ch[11] = buttonChannel(5);
    cfg.mapping.ch[12] = buttonChannel(6);
    cfg.mapping.ch[13] = buttonChannel(7);
    // Channels 15 and 16 stay unassigned (centre).
    return cfg;
}

const char* failsafeModeName(FailsafeMode mode) {
    switch (mode) {
        case FailsafeMode::Stop:
            return "stop";
        case FailsafeMode::Hold:
            return "hold";
        case FailsafeMode::Preset:
            return "preset";
    }
    return "?";
}

bool failsafeModeFromName(const char* name, FailsafeMode& out) {
    if (name == nullptr) {
        return false;
    }
    if (std::strcmp(name, "stop") == 0) {
        out = FailsafeMode::Stop;
        return true;
    }
    if (std::strcmp(name, "hold") == 0) {
        out = FailsafeMode::Hold;
        return true;
    }
    if (std::strcmp(name, "preset") == 0) {
        out = FailsafeMode::Preset;
        return true;
    }
    return false;
}

}  // namespace rc
