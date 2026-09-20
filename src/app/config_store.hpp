// SPDX-License-Identifier: MIT
// Live configuration with NVS persistence.
#pragma once

#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "rc/config.hpp"

namespace app {

class ConfigStore {
public:
    ConfigStore();

    /// Initialise NVS and load the stored configuration (or the defaults).
    void init();

    /// Copy of the whole live configuration.
    rc::Config get() const;
    /// Replace the live configuration (not persisted until save()).
    void set(const rc::Config& cfg);

    /// Cheap partial copies for the hot paths.
    void copyMapping(rc::MappingConfig& mapping, bool& log_input) const;
    void copyFailsafe(rc::FailsafeMode& mode, uint32_t& timeout_ms, rc::MappingConfig& mapping) const;
    void copySbus(rc::SbusConfig& sbus) const;
    void copyCppm(rc::CppmConfig& cppm) const;

    /// Persist the live configuration. Returns false on NVS error.
    bool save();
    /// Reload from NVS, discarding unsaved changes. Returns false when nothing is stored.
    bool load();
    /// Restore factory defaults in RAM and erase the stored blob.
    void resetToDefaults();

    bool loadedFromNvs() const { return loaded_from_nvs_; }

private:
    bool readBlob(rc::Config& out);

    mutable SemaphoreHandle_t mutex_ = nullptr;
    rc::Config config_ = rc::defaultConfig();
    bool loaded_from_nvs_ = false;
};

}  // namespace app
