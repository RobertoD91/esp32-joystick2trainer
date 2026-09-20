// SPDX-License-Identifier: MIT
#include "app/status_led.hpp"

#include "app/config_store.hpp"
#include "app/input_hub.hpp"
#include "driver/gpio.h"
#include "esp_log.h"

namespace app {

namespace {
const char* TAG = "led";
}

StatusLed::StatusLed(ConfigStore& config, InputHub& hub) : config_(config), hub_(hub) {}

void StatusLed::start() {
    gpio_ = config_.get().led_gpio;
    if (gpio_ < 0) {
        return;
    }
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << gpio_;
    io.mode = GPIO_MODE_OUTPUT;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&io) != ESP_OK) {
        ESP_LOGW(TAG, "GPIO%d cannot be used as an output", gpio_);
        gpio_ = -1;
        return;
    }
    xTaskCreate(&StatusLed::taskEntry, "led", 2048, this, 1, &task_);
    ESP_LOGI(TAG, "status LED on GPIO%d", gpio_);
}

void StatusLed::taskEntry(void* arg) {
    static_cast<StatusLed*>(arg)->run();
}

void StatusLed::run() {
    bool level = false;
    for (;;) {
        if (hub_.inputValid()) {
            level = true;
            gpio_set_level(static_cast<gpio_num_t>(gpio_), 1);
            vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            level = !level;
            gpio_set_level(static_cast<gpio_num_t>(gpio_), level ? 1 : 0);
            vTaskDelay(pdMS_TO_TICKS(hub_.connected() ? 500 : 150));
        }
    }
}

}  // namespace app
