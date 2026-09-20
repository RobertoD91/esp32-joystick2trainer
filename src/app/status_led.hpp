// SPDX-License-Identifier: MIT
// Optional status LED on a plain GPIO (active high).
//   off            : no LED configured
//   fast blink     : no joystick / no valid input (outputs in failsafe)
//   solid on       : joystick connected and input valid
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace app {

class ConfigStore;
class InputHub;

class StatusLed {
public:
    StatusLed(ConfigStore& config, InputHub& hub);
    void start();

private:
    static void taskEntry(void* arg);
    void run();

    ConfigStore& config_;
    InputHub& hub_;
    int gpio_ = -1;
    TaskHandle_t task_ = nullptr;
};

}  // namespace app
