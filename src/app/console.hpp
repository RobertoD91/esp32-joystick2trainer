// SPDX-License-Identifier: MIT
// Serial console (UART0) for configuration and diagnostics.
#pragma once

namespace app {

class ConfigStore;
class InputHub;
class UsbJoystick;
class SbusOutput;
class CppmOutput;

struct ConsoleContext {
    ConfigStore* config;
    InputHub* hub;
    UsbJoystick* usb;
    SbusOutput* sbus;
    CppmOutput* cppm;
};

/// Register the commands and start the REPL task. `ctx` must outlive the program.
void startConsole(ConsoleContext* ctx);

}  // namespace app
