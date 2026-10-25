// SPDX-License-Identifier: MIT
// Complete runtime configuration of the bridge. Plain data so it can be persisted as a blob.
#pragma once

#include <cstdint>

#include "rc/cppm.hpp"
#include "rc/mapping.hpp"
#include "rc/sbus.hpp"

// Compile-time defaults can be overridden from platformio.ini build_flags, e.g.
//   -DJ2T_SBUS_GPIO=17 -DJ2T_CPPM_GPIO=18
#ifndef J2T_SBUS_GPIO
#define J2T_SBUS_GPIO 17
#endif
#ifndef J2T_CPPM_GPIO
#define J2T_CPPM_GPIO 18
#endif
#ifndef J2T_LED_GPIO
#define J2T_LED_GPIO -1
#endif
#ifndef J2T_SBUS_PERIOD_US
#define J2T_SBUS_PERIOD_US 14000
#endif
#ifndef J2T_CPPM_CHANNELS
#define J2T_CPPM_CHANNELS 8
#endif
#ifndef J2T_CPPM_FRAME_US
#define J2T_CPPM_FRAME_US 22500
#endif

namespace rc {

/// Bump whenever the layout of Config changes; stored blobs with another version are discarded
/// (except for the one-step migrations handled by the config store).
constexpr uint32_t kConfigVersion = 5;

/// What the outputs do while no valid joystick input is available.
enum class FailsafeMode : uint8_t {
    Stop = 0,  ///< Stop emitting frames. EdgeTX detects the missing signal and returns control
               ///< to the master's own sticks (recommended for trainer use).
    Hold,      ///< Keep repeating the last good frame.
    Preset,    ///< Emit the per-channel failsafe values (SBUS frames carry the failsafe flag).
};

struct SbusConfig {
    bool enabled = true;
    int8_t gpio = J2T_SBUS_GPIO;
    /// Frame period in microseconds (14000 = standard, 7000 = "high speed").
    uint32_t period_us = J2T_SBUS_PERIOD_US;
    /// true = standard SBUS (inverted UART, idle low). false = non-inverted "SBUS" (idle high).
    bool inverted = true;
    /// Set the frame-lost/failsafe flag bits while the joystick input is not valid
    /// (hold/preset modes). EdgeTX discards flagged frames, so this is off by default;
    /// enable it for consumers that honour the flags (flight controllers).
    bool failsafe_flags = false;
};

struct CppmConfig {
    bool enabled = true;
    int8_t gpio = J2T_CPPM_GPIO;
    cppm::Config wave{};
};

/// Force feedback (USB HID PID devices only). The joystick's own centring spring is a
/// software effect that the host must create; without it the stick is loose.
struct FfbConfig {
    bool enabled = true;
    /// Strength of the centring spring, 0 (off) .. 100 %.
    uint8_t spring_pct = 60;
    /// Overall device gain, 0 .. 100 %.
    uint8_t gain_pct = 100;
    /// Dead band of the spring around the centre, 0 .. 50 % of travel.
    uint8_t deadband_pct = 0;
    /// Encode the spring the way the Linux driver does (Set Effect with Direction Enable set
    /// and the Axes Enable bits clear) instead of the per-axis form of the PID specification
    /// (Axes Enable X and Y, Direction Enable clear). Try 1 if a device centres one axis only.
    bool direction_form = false;
};

struct Config {
    uint32_t version = kConfigVersion;
    SbusConfig sbus{};
    CppmConfig cppm{};
    MappingConfig mapping{};
    FailsafeMode failsafe = FailsafeMode::Stop;
    /// Treat the input as lost when no HID report arrived for this long. 0 disables the
    /// check (recommended: many joysticks only report on change; USB disconnect is
    /// always detected regardless of this setting).
    uint32_t input_timeout_ms = 0;
    /// Optional status LED (-1 = none).
    int8_t led_gpio = J2T_LED_GPIO;
    /// Print decoded joystick input on the console (debug aid).
    bool log_input = false;
    /// Force feedback. Appended last so that older blobs can be migrated by prefix copy
    /// (version 3 ended before `ffb`, version 4 after its first four bytes).
    FfbConfig ffb{};
};

/// Factory defaults, including a mapping suited to a 4-axis/8-button stick such as the
/// Microsoft SideWinder Force Feedback 2.
Config defaultConfig();

const char* failsafeModeName(FailsafeMode mode);
bool failsafeModeFromName(const char* name, FailsafeMode& out);

}  // namespace rc
