// SPDX-License-Identifier: MIT
// Packs a CPPM waveform into RMT-style symbols (two level/duration halves per word,
// 15-bit durations). Hardware independent so the packing rules can be unit tested.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "rc/cppm.hpp"

namespace rc::pulse {

/// Mirrors the layout of an ESP32 `rmt_symbol_word_t` half: 15-bit duration + 1-bit level.
struct Half {
    uint16_t duration;  // 1..32767 ticks (0 is reserved by the RMT for "end of transmission")
    bool level;
};

struct Symbol {
    Half first;
    Half second;
};

/// Maximum duration a single RMT half symbol can hold.
constexpr uint32_t kMaxTicksPerHalf = 32767;

/// Worst case at 1 MHz: every segment (<= 60000 us) is split in 2 halves, so 2*kMaxSegments
/// halves = kMaxSegments symbols, plus one symbol of padding. Sized generously.
constexpr std::size_t kMaxSymbols = 2 * cppm::kMaxSegments + 1;

struct Packed {
    std::array<Symbol, kMaxSymbols> symbols{};
    std::size_t count = 0;
};

/// Convert a waveform (in microseconds) into RMT symbols at `ticks_per_us` resolution.
/// Long segments are split across several halves; a zero-length segment is dropped.
/// If the number of halves is odd, the last symbol is padded with a 1-tick half at the
/// idle level (negligible: 1 us at 1 MHz).
/// Returns false if the waveform does not fit in `Packed`.
bool pack(const cppm::Waveform& wf, uint32_t ticks_per_us, Packed& out);

}  // namespace rc::pulse
