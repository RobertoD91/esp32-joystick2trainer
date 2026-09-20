// SPDX-License-Identifier: MIT
// SBUS frame encoder/decoder (Futaba S.BUS, 25 bytes, 100000 baud 8E2, inverted UART).
// Pure C++, no hardware dependencies: the UART transport lives in the platform layer.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "rc/channels.hpp"

namespace rc::sbus {

constexpr std::size_t kFrameSize = 25;
constexpr uint8_t kHeader = 0x0F;
constexpr uint8_t kFooter = 0x00;

/// Standard SBUS 11-bit channel range, using the FrSky/EdgeTX convention
/// where 172 = 988 us, 992 = 1500 us and 1811 = 2012 us.
constexpr uint16_t kValueMin = 172;
constexpr uint16_t kValueCenter = 992;
constexpr uint16_t kValueMax = 1811;
constexpr uint16_t kValueAbsoluteMax = 2047;

/// Flag bits of byte 23.
constexpr uint8_t kFlagCh17 = 0x01;
constexpr uint8_t kFlagCh18 = 0x02;
constexpr uint8_t kFlagFrameLost = 0x04;
constexpr uint8_t kFlagFailsafe = 0x08;

/// Standard frame periods used by common transmitters.
constexpr uint32_t kPeriodNormalUs = 14000;
constexpr uint32_t kPeriodHighSpeedUs = 7000;

struct Flags {
    bool ch17 = false;
    bool ch18 = false;
    bool frame_lost = false;
    bool failsafe = false;
};

using Buffer = std::array<uint8_t, kFrameSize>;
using Values = std::array<uint16_t, kMaxChannels>;

/// Convert a servo pulse width (microseconds) to the 11-bit SBUS value.
/// Linear: sbus = (us - 880) * 8 / 5, clamped to 0..2047.
uint16_t fromUs(uint16_t us);

/// Convert an 11-bit SBUS value back to microseconds.
uint16_t toUs(uint16_t value);

/// Encode 16 channel values (11-bit each) and the flags into a 25-byte frame.
void encode(const Values& values, const Flags& flags, Buffer& out);

/// Encode a Frame expressed in microseconds. The failsafe/frame-lost flags come from `flags`.
void encodeFrame(const Frame& frame, const Flags& flags, Buffer& out);

/// Decode a 25-byte frame. Returns false when header/footer are invalid.
bool decode(const uint8_t* data, std::size_t len, Values& values, Flags& flags);

}  // namespace rc::sbus
