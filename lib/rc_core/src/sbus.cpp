// SPDX-License-Identifier: MIT
#include "rc/sbus.hpp"

namespace rc::sbus {

uint16_t fromUs(uint16_t us) {
    // 988 us -> 172.8, 1500 us -> 992, 2012 us -> 1811.2 (rounded to nearest).
    int32_t v = (static_cast<int32_t>(us) - 880) * 8;
    int32_t rounded = (v + 2) / 5;  // v is always >= 0 for us >= 880
    if (rounded < 0) {
        return 0;
    }
    if (rounded > kValueAbsoluteMax) {
        return kValueAbsoluteMax;
    }
    return static_cast<uint16_t>(rounded);
}

uint16_t toUs(uint16_t value) {
    if (value > kValueAbsoluteMax) {
        value = kValueAbsoluteMax;
    }
    // us = value * 5 / 8 + 880 (rounded)
    int32_t us = (static_cast<int32_t>(value) * 5 + 4) / 8 + 880;
    return static_cast<uint16_t>(us);
}

void encode(const Values& values, const Flags& flags, Buffer& out) {
    out.fill(0);
    out[0] = kHeader;

    // Pack 16 x 11-bit values LSB first into bytes 1..22.
    uint32_t bit_pos = 0;
    for (std::size_t ch = 0; ch < kMaxChannels; ++ch) {
        uint32_t value = values[ch] & 0x07FF;
        for (uint32_t b = 0; b < 11; ++b) {
            if (value & (1u << b)) {
                out[1 + (bit_pos >> 3)] |= static_cast<uint8_t>(1u << (bit_pos & 7));
            }
            ++bit_pos;
        }
    }

    uint8_t f = 0;
    if (flags.ch17) {
        f |= kFlagCh17;
    }
    if (flags.ch18) {
        f |= kFlagCh18;
    }
    if (flags.frame_lost) {
        f |= kFlagFrameLost;
    }
    if (flags.failsafe) {
        f |= kFlagFailsafe;
    }
    out[23] = f;
    out[24] = kFooter;
}

void encodeFrame(const Frame& frame, const Flags& flags, Buffer& out) {
    Values values{};
    for (std::size_t ch = 0; ch < kMaxChannels; ++ch) {
        values[ch] = fromUs(frame.us[ch]);
    }
    encode(values, flags, out);
}

bool decode(const uint8_t* data, std::size_t len, Values& values, Flags& flags) {
    if (data == nullptr || len < kFrameSize || data[0] != kHeader || data[24] != kFooter) {
        return false;
    }
    uint32_t bit_pos = 0;
    for (std::size_t ch = 0; ch < kMaxChannels; ++ch) {
        uint32_t value = 0;
        for (uint32_t b = 0; b < 11; ++b) {
            if (data[1 + (bit_pos >> 3)] & (1u << (bit_pos & 7))) {
                value |= (1u << b);
            }
            ++bit_pos;
        }
        values[ch] = static_cast<uint16_t>(value);
    }
    flags.ch17 = (data[23] & kFlagCh17) != 0;
    flags.ch18 = (data[23] & kFlagCh18) != 0;
    flags.frame_lost = (data[23] & kFlagFrameLost) != 0;
    flags.failsafe = (data[23] & kFlagFailsafe) != 0;
    return true;
}

}  // namespace rc::sbus
