// SPDX-License-Identifier: MIT
// Thread-safe meeting point between the USB input task and the output tasks.
// The USB task pushes decoded joystick states; the output tasks pull the frame to emit.
#pragma once

#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "rc/channels.hpp"
#include "rc/config.hpp"
#include "rc/input_state.hpp"
#include "rc/mapping.hpp"

namespace app {

class ConfigStore;

/// What an output should do right now.
struct OutputDecision {
    /// False when nothing should be emitted (failsafe mode "stop" with no valid input).
    bool emit = false;
    /// True when `frame` holds failsafe/held values instead of live input.
    bool failsafe = false;
    rc::Frame frame{};
};

class InputHub {
public:
    explicit InputHub(ConfigStore& config);

    /// Called by the USB layer for every decoded report.
    void onInput(const rc::InputState& state);
    /// Called by the USB layer when the joystick is ready (after descriptor parsing).
    void onConnected();
    /// Called by the USB layer when the joystick goes away.
    void onDisconnected();

    /// Called by the output tasks.
    OutputDecision decide();

    /// Status for the console.
    bool connected() const;
    bool inputValid() const;
    rc::InputState lastInput() const;
    rc::Frame lastFrame() const;
    uint32_t reportCount() const;
    int64_t lastReportTimeUs() const;

private:
    bool inputValidLocked(int64_t now_us, uint32_t timeout_ms) const;

    ConfigStore& config_;
    mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    rc::Mapper mapper_{};
    rc::InputState last_input_{};
    rc::Frame last_frame_ = rc::Frame::centered();
    bool connected_ = false;
    bool have_input_ = false;
    int64_t last_report_us_ = 0;
    uint32_t report_count_ = 0;
};

}  // namespace app
