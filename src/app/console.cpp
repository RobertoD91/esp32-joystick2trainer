// SPDX-License-Identifier: MIT
#include "app/console.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app/config_store.hpp"
#include "app/cppm_output.hpp"
#include "app/ffb.hpp"
#include "app/input_hub.hpp"
#include "app/sbus_output.hpp"
#include "app/usb_joystick.hpp"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rc/config_kv.hpp"

namespace app {

namespace {

const char* TAG = "console";
ConsoleContext* g_ctx = nullptr;

bool needsReboot(const char* key) {
    return std::strcmp(key, "sbus.enabled") == 0 || std::strcmp(key, "sbus.gpio") == 0 ||
           std::strcmp(key, "cppm.enabled") == 0 || std::strcmp(key, "cppm.gpio") == 0 ||
           std::strcmp(key, "led.gpio") == 0;
}

void printKv(const char* key, const char* value, void*) {
    std::printf("%-14s = %s\n", key, value);
}

int cmdShow(int argc, char** argv) {
    const rc::Config cfg = g_ctx->config->get();
    const char* prefix = argc > 1 ? argv[1] : nullptr;
    rc::kv::forEach(cfg, prefix, &printKv, nullptr);
    return 0;
}

int cmdGet(int argc, char** argv) {
    if (argc != 2) {
        std::printf("usage: get <key>\n");
        return 1;
    }
    char value[32];
    const rc::Config cfg = g_ctx->config->get();
    if (!rc::kv::get(cfg, argv[1], value, sizeof(value))) {
        std::printf("unknown key '%s'\n", argv[1]);
        return 1;
    }
    std::printf("%s = %s\n", argv[1], value);
    return 0;
}

int cmdSet(int argc, char** argv) {
    if (argc != 3) {
        std::printf("usage: set <key> <value>   (see 'show' for the keys)\n");
        return 1;
    }
    rc::Config cfg = g_ctx->config->get();
    char err[64] = {0};
    const rc::kv::Result r = rc::kv::set(cfg, argv[1], argv[2], err, sizeof(err));
    if (r != rc::kv::Result::Ok) {
        std::printf("error: %s\n", err);
        return 1;
    }
    g_ctx->config->set(cfg);
    if (std::strncmp(argv[1], "ffb.", 4) == 0 && g_ctx->ffb != nullptr) {
        g_ctx->ffb->applyConfig();
    }
    char value[32];
    rc::kv::get(cfg, argv[1], value, sizeof(value));
    std::printf("%s = %s%s\n", argv[1], value,
                needsReboot(argv[1]) ? "   (takes effect after 'save' and 'reboot')" : "   (not saved yet)");
    return 0;
}

int cmdSave(int, char**) {
    if (g_ctx->config->save()) {
        std::printf("configuration saved to NVS\n");
        return 0;
    }
    std::printf("save failed\n");
    return 1;
}

int cmdLoad(int, char**) {
    if (g_ctx->config->load()) {
        std::printf("configuration reloaded from NVS\n");
        return 0;
    }
    std::printf("nothing valid stored in NVS\n");
    return 1;
}

int cmdDefaults(int, char**) {
    g_ctx->config->resetToDefaults();
    std::printf("factory defaults restored and stored configuration erased (reboot to apply GPIO changes)\n");
    return 0;
}

int cmdReboot(int, char**) {
    std::printf("rebooting...\n");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return 0;
}

int cmdStatus(int, char**) {
    const DeviceInfo info = g_ctx->usb->deviceInfo();
    const rc::Config cfg = g_ctx->config->get();
    std::printf("joystick   : %s\n", info.valid ? "connected" : "none");
    if (info.valid) {
        std::printf("  device   : %04X:%04X \"%s\" (interface %u)\n", info.vid, info.pid, info.product, info.interface);
        std::printf("  reports  : %lu received, %lu rejected, last %lld ms ago\n",
                    static_cast<unsigned long>(g_ctx->hub->reportCount()),
                    static_cast<unsigned long>(g_ctx->usb->rejectedReports()),
                    static_cast<long long>((esp_timer_get_time() - g_ctx->hub->lastReportTimeUs()) / 1000));
    }
    std::printf("input valid: %s\n", g_ctx->hub->inputValid() ? "yes" : "no (failsafe)");
    std::printf("failsafe   : %s\n", rc::failsafeModeName(cfg.failsafe));
    std::printf("sbus       : %s, GPIO%d, %lu frames sent\n", g_ctx->sbus->running() ? "running" : "off", cfg.sbus.gpio,
                static_cast<unsigned long>(g_ctx->sbus->framesSent()));
    std::printf("cppm       : %s, GPIO%d, %lu frames sent\n", g_ctx->cppm->running() ? "running" : "off", cfg.cppm.gpio,
                static_cast<unsigned long>(g_ctx->cppm->framesSent()));
    if (g_ctx->ffb != nullptr) {
        const FfbController::Status f = g_ctx->ffb->status();
        if (!f.device_connected) {
            std::printf("ffb        : no device\n");
        } else if (!f.pid_device) {
            std::printf("ffb        : not a PID device (%s)\n", f.reason);
        } else {
            std::printf("ffb        : spring %u%% %s%s\n", f.spring_pct, f.spring_running ? "running" : "off",
                        f.last_error[0] ? " (last error: " : "");
            if (f.last_error[0]) {
                std::printf("%s)\n", f.last_error);
            }
        }
    }
    std::printf("config     : %s\n", g_ctx->config->loadedFromNvs() ? "from NVS" : "factory defaults");
    const rc::Frame f = g_ctx->hub->lastFrame();
    std::printf("channels   :");
    for (std::size_t i = 0; i < rc::kMaxChannels; ++i) {
        std::printf(" %u", f.us[i]);
    }
    std::printf("\n");
    return 0;
}

int cmdHid(int, char**) {
    const DeviceInfo info = g_ctx->usb->deviceInfo();
    if (!info.valid) {
        std::printf("no joystick connected\n");
        return 1;
    }
    const hid::ParsedDescriptor& d = g_ctx->usb->descriptor();
    std::printf("descriptor %u bytes, parse result: %s, report IDs: %s, application usage %04X:%04X\n",
                static_cast<unsigned>(info.descriptor_len), hid::parseResultName(info.parse_result),
                d.uses_report_ids ? "yes" : "no", d.application_usage_page, d.application_usage);
    std::printf("input fields:\n");
    for (std::size_t i = 0; i < d.field_count; ++i) {
        const hid::InputField& f = d.fields[i];
        std::printf("  id %3u  page %04X usage %04X%s  bits %3u..%-3u  x%-3u  logical %ld..%ld\n", f.report_id,
                    f.usage_page, f.usage, f.is_array ? " (array)" : "        ", f.bit_offset,
                    f.bit_offset + f.bit_size * f.count - 1, f.count, static_cast<long>(f.logical_min),
                    static_cast<long>(f.logical_max));
    }
    const hid::JoystickDecoder& dec = g_ctx->usb->decoder();
    std::printf("bindings:\n");
    for (std::size_t i = 0; i < rc::kAxisCount; ++i) {
        const auto& b = dec.axisBinding(static_cast<rc::Axis>(i));
        if (b.present) {
            std::printf("  axis:%-11s <- report %u bits %u..%u\n", rc::axisName(static_cast<rc::Axis>(i)),
                        b.field.report_id, b.field.bit_offset, b.field.bit_offset + b.field.bit_size - 1);
        }
    }
    std::printf("  hat: %s, buttons: %u\n", dec.hatBinding().present ? "yes" : "no",
                static_cast<unsigned>(dec.buttonCount()));
    return 0;
}

int cmdMonitor(int argc, char** argv) {
    int seconds = argc > 1 ? std::atoi(argv[1]) : 5;
    if (seconds < 1) {
        seconds = 1;
    }
    if (seconds > 600) {
        seconds = 600;
    }
    std::printf("monitoring for %d s (values: axes -1..1, hat, buttons, channels in us)\n", seconds);
    const int64_t end = esp_timer_get_time() + static_cast<int64_t>(seconds) * 1000000;
    while (esp_timer_get_time() < end) {
        const rc::InputState s = g_ctx->hub->lastInput();
        const rc::Frame f = g_ctx->hub->lastFrame();
        std::printf("%s |", g_ctx->hub->inputValid() ? "OK" : "FS");
        for (std::size_t i = 0; i < rc::kAxisCount; ++i) {
            if (s.axis_present[i]) {
                std::printf(" %s=%+.2f", rc::axisName(static_cast<rc::Axis>(i)), static_cast<double>(s.axis[i]));
            }
        }
        std::printf(" hat=%d btn=%08lX |", s.hat, static_cast<unsigned long>(s.buttons));
        for (std::size_t i = 0; i < rc::kMaxChannels; ++i) {
            std::printf(" %u", f.us[i]);
        }
        std::printf("\n");
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return 0;
}

int cmdFfb(int argc, char** argv) {
    if (g_ctx->ffb == nullptr) {
        std::printf("force feedback not available\n");
        return 1;
    }
    if (argc >= 2 && std::strcmp(argv[1], "test") == 0) {
        const int ms = argc >= 3 ? std::atoi(argv[2]) : 600;
        if (!g_ctx->ffb->requestRumble(ms > 0 ? static_cast<uint32_t>(ms) : 600)) {
            std::printf("no PID device connected\n");
            return 1;
        }
        std::printf("vibration test queued (%d ms)\n", ms > 0 ? ms : 600);
        return 0;
    }
    if (argc >= 2 && std::strcmp(argv[1], "apply") == 0) {
        g_ctx->ffb->applyConfig();
        std::printf("force feedback settings re-applied\n");
        return 0;
    }
    if (argc >= 2) {
        std::printf("usage: ffb [test [ms] | apply]\n");
        return 1;
    }
    const FfbController::Status f = g_ctx->ffb->status();
    const rc::Config cfg = g_ctx->config->get();
    std::printf("config     : enabled %d, spring %u%%, gain %u%%, deadband %u%%\n", cfg.ffb.enabled, cfg.ffb.spring_pct,
                cfg.ffb.gain_pct, cfg.ffb.deadband_pct);
    if (!f.device_connected) {
        std::printf("device     : none\n");
        return 0;
    }
    if (!f.pid_device) {
        std::printf("device     : connected, no PID force feedback (%s)\n", f.reason);
        return 0;
    }
    std::printf("device     : PID, output reports via %s, init %s\n",
                f.out_endpoint ? "interrupt OUT endpoint" : "control SET_REPORT", f.initialised ? "ok" : "FAILED");
    std::printf("spring     : %u%% on block %d, %s\n", f.spring_pct, f.spring_block,
                f.spring_running ? "running" : "stopped");
    if (f.state_valid) {
        std::printf(
            "state      : actuators %s, power %s, safety switch %s, override %s, paused %s, playing %s "
            "(block %d), %lu reports\n",
            f.state.actuators_enabled ? "enabled" : "disabled", f.state.actuator_power ? "ok" : "OFF",
            f.state.safety_switch ? "on" : "off", f.state.actuator_override ? "on" : "off",
            f.state.device_paused ? "yes" : "no", f.state.effect_playing ? "yes" : "no", f.state.effect_block_index,
            static_cast<unsigned long>(f.state_reports));
    } else {
        std::printf("state      : no PID state report received yet\n");
    }
    if (f.last_error[0]) {
        std::printf("last error : %s\n", f.last_error);
    }
    std::printf("commands   : ffb test [ms]   set ffb.spring <0-100>   set ffb.gain <0-100>   set ffb.enabled 0/1\n");
    return 0;
}

void registerCommand(const char* name, const char* help, const char* hint, esp_console_cmd_func_t func) {
    esp_console_cmd_t cmd = {};
    cmd.command = name;
    cmd.help = help;
    cmd.hint = hint;
    cmd.func = func;
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

}  // namespace

void startConsole(ConsoleContext* ctx) {
    g_ctx = ctx;
    esp_console_repl_t* repl = nullptr;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "j2t>";
    repl_cfg.max_cmdline_length = 128;
    repl_cfg.task_stack_size = 6144;

    esp_console_register_help_command();
    registerCommand("show", "Print configuration keys (optionally filtered by prefix, e.g. 'show ch3.')", "[prefix]",
                    &cmdShow);
    registerCommand("get", "Read one configuration key", "<key>", &cmdGet);
    registerCommand("set", "Change one configuration key (RAM only until 'save')", "<key> <value>", &cmdSet);
    registerCommand("save", "Persist the configuration to NVS", nullptr, &cmdSave);
    registerCommand("load", "Discard unsaved changes and reload from NVS", nullptr, &cmdLoad);
    registerCommand("defaults", "Restore factory defaults", nullptr, &cmdDefaults);
    registerCommand("status", "Joystick, outputs and current channel values", nullptr, &cmdStatus);
    registerCommand("hid", "Dump the parsed HID report descriptor and axis bindings", nullptr, &cmdHid);
    registerCommand("monitor", "Print live input and channels for N seconds", "[seconds]", &cmdMonitor);
    registerCommand("ffb", "Force feedback status, 'ffb test [ms]' vibration test, 'ffb apply'", "[test [ms]|apply]",
                    &cmdFfb);
    registerCommand("reboot", "Restart the board", nullptr, &cmdReboot);

    ESP_ERROR_CHECK(esp_console_new_repl_stdio(&repl_cfg, &repl));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    ESP_LOGI(TAG, "type 'help' for the list of commands");
}

}  // namespace app
