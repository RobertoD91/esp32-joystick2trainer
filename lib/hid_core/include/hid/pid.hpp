// SPDX-License-Identifier: MIT
// USB HID PID (Physical Interface Device) force-feedback client, descriptor driven.
//
// The report layouts are taken from the device's report descriptor at run time, so the same
// code drives any PID-compliant joystick (Microsoft SideWinder Force Feedback 2, Logitech
// and Thrustmaster PID sticks, ...). Only the subset needed by this project is implemented:
// device control/gain, effect creation (block load), set effect, condition (spring/damper)
// and periodic (vibration) parameters, effect start/stop/free, and the PID state report.
//
// Pure C++: the USB transport is abstracted (see Transport) so everything is unit tested on
// the host against the real SideWinder descriptor.
#pragma once

#include <cstddef>
#include <cstdint>

#include "hid/report_descriptor.hpp"

namespace hid::pid {

// PID usage page (0x0F) usages used here.
constexpr uint16_t kUsageSetEffectReport = 0x21;
constexpr uint16_t kUsageEffectBlockIndex = 0x22;
constexpr uint16_t kUsageParameterBlockOffset = 0x23;
constexpr uint16_t kUsageEffectType = 0x25;
constexpr uint16_t kUsageEtConstantForce = 0x26;
constexpr uint16_t kUsageEtRamp = 0x27;
constexpr uint16_t kUsageEtCustomForce = 0x28;
constexpr uint16_t kUsageEtSquare = 0x30;
constexpr uint16_t kUsageEtSine = 0x31;
constexpr uint16_t kUsageEtTriangle = 0x32;
constexpr uint16_t kUsageEtSawtoothUp = 0x33;
constexpr uint16_t kUsageEtSawtoothDown = 0x34;
constexpr uint16_t kUsageEtSpring = 0x40;
constexpr uint16_t kUsageEtDamper = 0x41;
constexpr uint16_t kUsageEtInertia = 0x42;
constexpr uint16_t kUsageEtFriction = 0x43;
constexpr uint16_t kUsageDuration = 0x50;
constexpr uint16_t kUsageSamplePeriod = 0x51;
constexpr uint16_t kUsageGain = 0x52;
constexpr uint16_t kUsageTriggerButton = 0x53;
constexpr uint16_t kUsageTriggerRepeatInterval = 0x54;
constexpr uint16_t kUsageAxesEnable = 0x55;
constexpr uint16_t kUsageDirectionEnable = 0x56;
constexpr uint16_t kUsageDirection = 0x57;
constexpr uint16_t kUsageTypeSpecificBlockOffset = 0x58;
constexpr uint16_t kUsageSetEnvelopeReport = 0x5A;
constexpr uint16_t kUsageAttackLevel = 0x5B;
constexpr uint16_t kUsageAttackTime = 0x5C;
constexpr uint16_t kUsageFadeLevel = 0x5D;
constexpr uint16_t kUsageFadeTime = 0x5E;
constexpr uint16_t kUsageSetConditionReport = 0x5F;
constexpr uint16_t kUsageCpOffset = 0x60;
constexpr uint16_t kUsagePositiveCoefficient = 0x61;
constexpr uint16_t kUsageNegativeCoefficient = 0x62;
constexpr uint16_t kUsagePositiveSaturation = 0x63;
constexpr uint16_t kUsageNegativeSaturation = 0x64;
constexpr uint16_t kUsageDeadBand = 0x65;
constexpr uint16_t kUsageSetPeriodicReport = 0x6E;
constexpr uint16_t kUsageOffset = 0x6F;
constexpr uint16_t kUsageMagnitude = 0x70;
constexpr uint16_t kUsagePhase = 0x71;
constexpr uint16_t kUsagePeriod = 0x72;
constexpr uint16_t kUsageSetConstantForceReport = 0x73;
constexpr uint16_t kUsageEffectOperationReport = 0x77;
constexpr uint16_t kUsageEffectOperation = 0x78;
constexpr uint16_t kUsageOpEffectStart = 0x79;
constexpr uint16_t kUsageOpEffectStartSolo = 0x7A;
constexpr uint16_t kUsageOpEffectStop = 0x7B;
constexpr uint16_t kUsageLoopCount = 0x7C;
constexpr uint16_t kUsageDeviceGainReport = 0x7D;
constexpr uint16_t kUsageDeviceGain = 0x7E;
constexpr uint16_t kUsagePidPoolReport = 0x7F;
constexpr uint16_t kUsageRamPoolSize = 0x80;
constexpr uint16_t kUsageSimultaneousEffectsMax = 0x83;
constexpr uint16_t kUsageDeviceManagedPool = 0xA9;
constexpr uint16_t kUsageSharedParameterBlocks = 0xAA;
constexpr uint16_t kUsageBlockLoadReport = 0x89;
constexpr uint16_t kUsageBlockLoadStatus = 0x8B;
constexpr uint16_t kUsageBlockLoadSuccess = 0x8C;
constexpr uint16_t kUsageBlockLoadFull = 0x8D;
constexpr uint16_t kUsageBlockLoadError = 0x8E;
constexpr uint16_t kUsageBlockFreeReport = 0x90;
constexpr uint16_t kUsagePidStateReport = 0x92;
constexpr uint16_t kUsageEffectPlaying = 0x94;
constexpr uint16_t kUsageDeviceControl = 0x96;
constexpr uint16_t kUsageDcEnableActuators = 0x97;
constexpr uint16_t kUsageDcDisableActuators = 0x98;
constexpr uint16_t kUsageDcStopAllEffects = 0x99;
constexpr uint16_t kUsageDcDeviceReset = 0x9A;
constexpr uint16_t kUsageDcDevicePause = 0x9B;
constexpr uint16_t kUsageDcDeviceContinue = 0x9C;
constexpr uint16_t kUsageDevicePaused = 0x9F;
constexpr uint16_t kUsageActuatorsEnabled = 0xA0;
constexpr uint16_t kUsageSafetySwitch = 0xA4;
constexpr uint16_t kUsageActuatorOverrideSwitch = 0xA5;
constexpr uint16_t kUsageActuatorPower = 0xA6;
constexpr uint16_t kUsageStartDelay = 0xA7;
constexpr uint16_t kUsageCreateNewEffectReport = 0xAB;
constexpr uint16_t kUsageRamPoolAvailable = 0xAC;

constexpr std::size_t kMaxReportBytes = 64;

/// A report ready to be sent (or just received): the report ID byte (when the device uses
/// IDs) followed by the payload.
struct Report {
    uint8_t data[kMaxReportBytes] = {0};
    std::size_t len = 0;
    uint8_t id = 0;
};

/// USB transport the controller talks through. Implemented with the ESP-IDF HID host driver
/// on the target and with a fake in the unit tests. All calls are blocking.
class Transport {
public:
    virtual ~Transport() = default;
    virtual bool sendOutput(const Report& report) = 0;
    virtual bool setFeature(const Report& report) = 0;
    /// Read a feature report. `out.len` is the number of bytes received.
    virtual bool getFeature(uint8_t report_id, std::size_t expected_len, Report& out) = 0;
    virtual void delayMs(uint32_t ms) = 0;
};

/// What the device's PID state input report says.
struct State {
    bool device_paused = false;
    bool actuators_enabled = false;
    bool safety_switch = false;
    bool actuator_override = false;
    bool actuator_power = false;
    bool effect_playing = false;
    int effect_block_index = 0;
};

/// Set Effect parameters. The defaults encode the per-axis form of the PID specification
/// (Axes Enable X and Y set, Direction Enable clear): the right one for a condition effect
/// with one parameter block per axis. The Linux driver instead sends every effect with
/// Direction Enable set and the Axes Enable bits clear (`linuxDirectionForm()`).
struct EffectParams {
    uint16_t type = kUsageEtSpring;
    uint32_t duration_ms = 0;  ///< 0 = infinite
    float gain = 1.0f;         ///< 0..1
    bool direction_enable = false;
    float direction = 0.0f;  ///< 0..1 = 0..360 degrees (PID polar direction, 0 = north)
    bool axis_x = true;
    bool axis_y = true;
    uint32_t start_delay_ms = 0;

    /// Switch to the form the Linux hid-pidff driver sends (Direction Enable, no axes).
    EffectParams& linuxDirectionForm(float dir = 0.0f) {
        direction_enable = true;
        direction = dir;
        axis_x = false;
        axis_y = false;
        return *this;
    }
};

/// Contents of the PID Pool feature report (device memory for effects).
struct PoolInfo {
    uint32_t ram_pool_size = 0;     ///< bytes, 0 when not reported
    uint32_t simultaneous_max = 0;  ///< effects playing at once, 0 when not reported
    bool has_device_managed_flag = false;
    bool device_managed = false;  ///< the device allocates parameter blocks itself
    bool shared_parameter_blocks = false;
};

struct Condition {
    float center = 0.0f;          ///< -1..1
    float positive_coeff = 1.0f;  ///< -1..1
    float negative_coeff = 1.0f;  ///< -1..1
    float positive_sat = 1.0f;    ///< 0..1
    float negative_sat = 1.0f;    ///< 0..1
    float dead_band = 0.0f;       ///< 0..1
};

struct Periodic {
    float magnitude = 1.0f;  ///< 0..1
    float offset = 0.0f;     ///< -1..1
    float phase = 0.0f;      ///< 0..1
    uint32_t period_ms = 50;
};

/// Descriptor-derived view of a PID device: locates the reports and fields and builds raw
/// reports. Stateless apart from the layout.
class Device {
public:
    /// Locate the PID reports. Returns false (with a reason) when the descriptor is not a
    /// usable PID device. `desc` must outlive the Device (fields are referenced by pointer).
    bool configure(const ParsedDescriptor& desc, char* reason, std::size_t reason_len);

    bool configured() const { return configured_; }
    bool supportsEffect(uint16_t effect_type_usage) const;
    bool hasConditionReport() const { return set_condition_id_ >= 0; }
    bool hasPeriodicReport() const { return set_periodic_id_ >= 0; }
    bool hasDeviceGain() const { return device_gain_ != nullptr; }
    bool hasStateReport() const { return state_report_id_ >= 0; }
    bool hasPoolReport() const { return pool_id_ >= 0; }
    int blockIndexMin() const { return block_index_min_; }
    int blockIndexMax() const { return block_index_max_; }
    uint8_t stateReportId() const { return static_cast<uint8_t>(state_report_id_); }
    uint8_t poolReportId() const { return static_cast<uint8_t>(pool_id_); }
    /// Length of the PID Pool feature report including the ID byte (0 when absent).
    std::size_t poolReportLen() const;
    /// Decode a PID Pool feature report (report bytes including the ID byte).
    bool parsePool(const Report& in, PoolInfo& out) const;

    // Report builders (return false when the report/field is missing).
    bool buildDeviceControl(uint16_t dc_usage, Report& out) const;
    bool buildDeviceGain(float gain, Report& out) const;
    bool buildCreateNewEffect(uint16_t effect_type_usage, Report& out) const;
    bool parseBlockLoad(const Report& in, int& block_index, uint16_t& status_usage, uint32_t& ram_available) const;
    uint8_t blockLoadReportId() const { return static_cast<uint8_t>(block_load_id_); }
    std::size_t blockLoadReportLen() const;
    bool buildSetEffect(int block_index, const EffectParams& p, Report& out) const;
    bool buildSetCondition(int block_index, int axis, const Condition& c, Report& out) const;
    bool buildSetPeriodic(int block_index, const Periodic& p, Report& out) const;
    bool buildEffectOperation(int block_index, uint16_t op_usage, int loop_count, Report& out) const;
    bool buildBlockFree(int block_index, Report& out) const;

    /// Decode the PID state input report (report bytes including the ID byte).
    bool decodeState(const uint8_t* report, std::size_t len, State& out) const;

    /// Convert milliseconds to the field's time unit (HID unit exponent aware).
    static int32_t timeValue(const ReportField& f, uint32_t ms);

private:
    const ReportField* out(uint16_t usage, int report_id, std::size_t nth = 0) const;
    const ReportField* outInCollection(uint16_t page, uint16_t usage, int report_id, uint16_t collection_usage,
                                       std::size_t nth = 0) const;
    const ReportField* feature(uint16_t usage, int report_id, std::size_t nth = 0) const;
    const ReportField* input(uint16_t usage, int report_id) const;
    bool begin(ReportType type, int report_id, Report& out) const;
    static bool put(Report& r, const ReportField* f, int32_t value, std::size_t element = 0);
    static bool putOptional(Report& r, const ReportField* f, int32_t value);

    const ParsedDescriptor* desc_ = nullptr;
    bool configured_ = false;
    int set_effect_id_ = -1;
    int set_condition_id_ = -1;
    int set_periodic_id_ = -1;
    int effect_operation_id_ = -1;
    int device_control_id_ = -1;
    int device_gain_id_ = -1;
    int block_free_id_ = -1;
    int create_new_effect_id_ = -1;
    int block_load_id_ = -1;
    int state_report_id_ = -1;
    int pool_id_ = -1;
    const ReportField* device_gain_ = nullptr;
    const ReportField* device_control_ = nullptr;
    const ReportField* create_effect_type_ = nullptr;
    const ReportField* set_effect_type_ = nullptr;
    const ReportField* block_load_index_ = nullptr;
    const ReportField* block_load_status_ = nullptr;
    const ReportField* operation_ = nullptr;
    int block_index_min_ = 1;
    int block_index_max_ = 1;
};

/// Blocking sequences on top of Device + Transport (what a driver does).
class Controller {
public:
    Controller(const Device& device, Transport& transport) : device_(device), transport_(transport) {}

    /// Device reset, stop all effects, enable actuators, full device gain.
    bool initialise();
    bool deviceControl(uint16_t dc_usage);
    bool setDeviceGain(float gain);
    /// Read the PID Pool feature report (diagnostics; false when absent or unanswered).
    bool readPool(PoolInfo& out);
    /// Create an effect block; returns the block index or -1. `status` receives the PID
    /// block-load status usage (success/full/error) when available.
    int createEffect(uint16_t effect_type_usage, uint16_t* status = nullptr);
    bool freeEffect(int block_index);
    bool startEffect(int block_index, int loop_count = 1);
    bool stopEffect(int block_index);
    /// Upload a two-axis spring (or damper/friction/inertia) effect: set effect + conditions.
    bool uploadCondition(int block_index, uint16_t effect_type_usage, const EffectParams& e, const Condition& c);
    /// Upload a periodic (vibration) effect: set effect + periodic parameters.
    bool uploadPeriodic(int block_index, uint16_t effect_type_usage, const EffectParams& e, const Periodic& p);

private:
    const Device& device_;
    Transport& transport_;
};

const char* effectTypeName(uint16_t usage);
const char* blockLoadStatusName(uint16_t usage);

}  // namespace hid::pid
