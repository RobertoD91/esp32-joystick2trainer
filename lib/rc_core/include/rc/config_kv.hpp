// SPDX-License-Identifier: MIT
// Key/value view of the configuration, shared by the serial console and the tests.
//
// Keys:
//   sbus.enabled sbus.gpio sbus.period sbus.inverted sbus.flags
//   cppm.enabled cppm.gpio cppm.channels cppm.frame cppm.pulse cppm.inverted cppm.min cppm.max
//   cppm.sync_min cppm.sync_max
//   failsafe.mode (stop|hold|preset)  input.timeout (ms)  led.gpio  log.input
//   chN.src (none|const|axis:<name>|btn:<1-32>|hatx|haty)  N = 1..16
//   chN.invert chN.toggle chN.deadband chN.expo chN.min chN.max chN.trim chN.failsafe
#pragma once

#include <cstddef>

#include "rc/config.hpp"

namespace rc::kv {

enum class Result {
    Ok,
    UnknownKey,
    BadValue,
};

/// Set one key. On error, `err` (if not null) receives a short human readable reason.
Result set(Config& cfg, const char* key, const char* value, char* err, std::size_t err_len);

/// Read one key as text. Returns false for unknown keys.
bool get(const Config& cfg, const char* key, char* out, std::size_t out_len);

using Visitor = void (*)(const char* key, const char* value, void* ctx);

/// Enumerate every key with its current value (in a stable order).
/// `prefix` (optional) restricts the output to keys starting with it, e.g. "ch3." or "sbus".
void forEach(const Config& cfg, const char* prefix, Visitor visitor, void* ctx);

/// Render a channel source as text ("axis:x", "btn:3", ...).
void formatSource(const ChannelConfig& ch, char* out, std::size_t out_len);

/// Parse a channel source text. Returns false when invalid.
bool parseSource(const char* text, ChannelConfig& ch);

}  // namespace rc::kv
