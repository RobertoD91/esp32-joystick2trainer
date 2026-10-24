// SPDX-License-Identifier: MIT
#include "app/config_store.hpp"

#include <cstddef>
#include <cstring>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace app {

namespace {
const char* TAG = "config";
constexpr const char* kNamespace = "j2t";
constexpr const char* kKey = "cfg";

class Lock {
public:
    explicit Lock(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(m_); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;

private:
    SemaphoreHandle_t m_;
};
}  // namespace

ConfigStore::ConfigStore() {
    mutex_ = xSemaphoreCreateMutex();
}

void ConfigStore::init() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erasing (%s)", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    rc::Config stored;
    if (readBlob(stored)) {
        Lock lock(mutex_);
        config_ = stored;
        loaded_from_nvs_ = true;
        ESP_LOGI(TAG, "configuration loaded from NVS");
    } else {
        ESP_LOGI(TAG, "using factory defaults (nothing valid stored)");
    }
}

bool ConfigStore::readBlob(rc::Config& out) {
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    // Version 3 blobs are the current layout without the trailing FfbConfig: migrate by
    // prefix copy so that user settings survive the firmware update. The legacy struct ended
    // right before `ffb` and was padded to the struct alignment.
    static_assert(offsetof(rc::Config, ffb) + sizeof(rc::FfbConfig) <= sizeof(rc::Config),
                  "FfbConfig must stay the last member of rc::Config");
    constexpr std::size_t kLegacyV3Prefix = offsetof(rc::Config, ffb);
    constexpr std::size_t kLegacyV3Size =
        (kLegacyV3Prefix + alignof(rc::Config) - 1) / alignof(rc::Config) * alignof(rc::Config);
    constexpr uint32_t kLegacyV3Version = 3;

    size_t size = 0;
    esp_err_t err = nvs_get_blob(handle, kKey, nullptr, &size);
    bool ok = false;
    if (err == ESP_OK && (size == sizeof(rc::Config) || size == kLegacyV3Size)) {
        alignas(rc::Config) uint8_t raw[sizeof(rc::Config)] = {0};
        err = nvs_get_blob(handle, kKey, raw, &size);
        uint32_t version = 0;
        std::memcpy(&version, raw, sizeof(version));
        if (err == ESP_OK && size == sizeof(rc::Config) && version == rc::kConfigVersion) {
            std::memcpy(&out, raw, sizeof(out));
            ok = true;
        } else if (err == ESP_OK && size == kLegacyV3Size && version == kLegacyV3Version) {
            rc::Config migrated = rc::defaultConfig();
            std::memcpy(&migrated, raw, kLegacyV3Prefix);
            migrated.version = rc::kConfigVersion;
            out = migrated;
            ok = true;
            ESP_LOGW(TAG, "stored configuration migrated from version %lu (force feedback defaults added)",
                     static_cast<unsigned long>(kLegacyV3Version));
        } else {
            ESP_LOGW(TAG, "stored configuration ignored (version %lu, expected %lu)",
                     static_cast<unsigned long>(version), static_cast<unsigned long>(rc::kConfigVersion));
        }
    } else if (err == ESP_OK) {
        ESP_LOGW(TAG, "stored configuration has the wrong size (%u vs %u)", static_cast<unsigned>(size),
                 static_cast<unsigned>(sizeof(rc::Config)));
    }
    nvs_close(handle);
    return ok;
}

rc::Config ConfigStore::get() const {
    Lock lock(mutex_);
    return config_;
}

void ConfigStore::set(const rc::Config& cfg) {
    Lock lock(mutex_);
    config_ = cfg;
    config_.version = rc::kConfigVersion;
}

void ConfigStore::copyMapping(rc::MappingConfig& mapping, bool& log_input) const {
    Lock lock(mutex_);
    mapping = config_.mapping;
    log_input = config_.log_input;
}

void ConfigStore::copyFailsafe(rc::FailsafeMode& mode, uint32_t& timeout_ms, rc::MappingConfig& mapping) const {
    Lock lock(mutex_);
    mode = config_.failsafe;
    timeout_ms = config_.input_timeout_ms;
    mapping = config_.mapping;
}

void ConfigStore::copySbus(rc::SbusConfig& sbus) const {
    Lock lock(mutex_);
    sbus = config_.sbus;
}

void ConfigStore::copyCppm(rc::CppmConfig& cppm) const {
    Lock lock(mutex_);
    cppm = config_.cppm;
}

bool ConfigStore::save() {
    const rc::Config snapshot = get();
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return false;
    }
    err = nvs_set_blob(handle, kKey, &snapshot, sizeof(snapshot));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saving failed: %s", esp_err_to_name(err));
        return false;
    }
    loaded_from_nvs_ = true;
    return true;
}

bool ConfigStore::load() {
    rc::Config stored;
    if (!readBlob(stored)) {
        return false;
    }
    set(stored);
    return true;
}

void ConfigStore::resetToDefaults() {
    set(rc::defaultConfig());
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_key(handle, kKey);
        nvs_commit(handle);
        nvs_close(handle);
    }
    loaded_from_nvs_ = false;
}

}  // namespace app
