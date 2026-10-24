// SPDX-License-Identifier: MIT
#include "hid/pid.hpp"

#include <cstdio>
#include <cstring>

#include "hid/bitfield.hpp"

namespace hid::pid {

namespace {

constexpr std::size_t kBlockLoadPolls = 20;
constexpr uint32_t kBlockLoadPollDelayMs = 5;
constexpr uint32_t kResetSettleMs = 20;

/// Find the array field of `type` whose enclosing collection is `collection_usage`
/// (PID lists such as Effect Type, Device Control, Block Load Status, Effect Operation).
const ReportField* findArrayInCollection(const ParsedDescriptor& d, ReportType type, uint16_t collection_usage,
                                         int& report_id_out) {
    const ReportField* list = d.fieldsOf(type);
    const std::size_t n = d.fieldCountOf(type);
    for (std::size_t i = 0; i < n; ++i) {
        const ReportField& f = list[i];
        if (f.is_array && f.collection_page == kPagePid && f.collection_usage == collection_usage) {
            report_id_out = f.report_id;
            return &f;
        }
    }
    return nullptr;
}

/// Report ID of the report of `type` that contains a PID field with `usage` (first match).
int reportWith(const ParsedDescriptor& d, ReportType type, uint16_t usage) {
    const ReportField* f = d.find(type, kPagePid, usage);
    return f ? f->report_id : -1;
}

int reportWithCollection(const ParsedDescriptor& d, ReportType type, uint16_t collection_usage) {
    const ReportField* list = d.fieldsOf(type);
    const std::size_t n = d.fieldCountOf(type);
    for (std::size_t i = 0; i < n; ++i) {
        if (list[i].collection_page == kPagePid && list[i].collection_usage == collection_usage) {
            return list[i].report_id;
        }
    }
    return -1;
}

uint16_t usageForArrayValue(const ParsedDescriptor& d, const ReportField& f, int32_t value) {
    const int32_t idx = value - f.logical_min;
    if (idx < 0) {
        return 0;
    }
    if (f.usage_list_count > 0) {
        if (static_cast<std::size_t>(idx) < f.usage_list_count) {
            return d.usage_pool[f.usage_list_start + static_cast<std::size_t>(idx)];
        }
        return 0;
    }
    const int32_t usage = static_cast<int32_t>(f.usage) + idx;
    return usage <= f.usage_max ? static_cast<uint16_t>(usage) : 0;
}

void setReason(char* reason, std::size_t len, const char* text) {
    if (reason != nullptr && len > 0) {
        std::snprintf(reason, len, "%s", text);
    }
}

}  // namespace

const char* effectTypeName(uint16_t usage) {
    switch (usage) {
        case kUsageEtConstantForce:
            return "constant";
        case kUsageEtRamp:
            return "ramp";
        case kUsageEtCustomForce:
            return "custom";
        case kUsageEtSquare:
            return "square";
        case kUsageEtSine:
            return "sine";
        case kUsageEtTriangle:
            return "triangle";
        case kUsageEtSawtoothUp:
            return "sawtooth-up";
        case kUsageEtSawtoothDown:
            return "sawtooth-down";
        case kUsageEtSpring:
            return "spring";
        case kUsageEtDamper:
            return "damper";
        case kUsageEtInertia:
            return "inertia";
        case kUsageEtFriction:
            return "friction";
        default:
            return "?";
    }
}

const char* blockLoadStatusName(uint16_t usage) {
    switch (usage) {
        case kUsageBlockLoadSuccess:
            return "success";
        case kUsageBlockLoadFull:
            return "full";
        case kUsageBlockLoadError:
            return "error";
        default:
            return "?";
    }
}

// ------------------------------------------------------------------------------------ Device

const ReportField* Device::out(uint16_t usage, int report_id, std::size_t nth) const {
    return desc_ ? desc_->find(ReportType::Output, kPagePid, usage, report_id, nth) : nullptr;
}

const ReportField* Device::outInCollection(uint16_t page, uint16_t usage, int report_id, uint16_t collection_usage,
                                           std::size_t nth) const {
    if (desc_ == nullptr) {
        return nullptr;
    }
    std::size_t seen = 0;
    for (std::size_t i = 0; i < desc_->out_field_count; ++i) {
        const ReportField& f = desc_->out_fields[i];
        if (f.report_id == report_id && f.usage_page == page && f.usage == usage && f.collection_page == kPagePid &&
            f.collection_usage == collection_usage) {
            if (seen == nth) {
                return &f;
            }
            ++seen;
        }
    }
    return nullptr;
}

const ReportField* Device::feature(uint16_t usage, int report_id, std::size_t nth) const {
    return desc_ ? desc_->find(ReportType::Feature, kPagePid, usage, report_id, nth) : nullptr;
}

const ReportField* Device::input(uint16_t usage, int report_id) const {
    return desc_ ? desc_->find(ReportType::Input, kPagePid, usage, report_id) : nullptr;
}

bool Device::configure(const ParsedDescriptor& desc, char* reason, std::size_t reason_len) {
    *this = Device{};
    desc_ = &desc;

    set_effect_id_ = reportWithCollection(desc, ReportType::Output, kUsageSetEffectReport);
    set_condition_id_ = reportWithCollection(desc, ReportType::Output, kUsageSetConditionReport);
    set_periodic_id_ = reportWithCollection(desc, ReportType::Output, kUsageSetPeriodicReport);
    effect_operation_id_ = reportWithCollection(desc, ReportType::Output, kUsageEffectOperationReport);
    device_gain_id_ = reportWithCollection(desc, ReportType::Output, kUsageDeviceGainReport);
    block_free_id_ = reportWithCollection(desc, ReportType::Output, kUsageBlockFreeReport);
    create_new_effect_id_ = reportWithCollection(desc, ReportType::Feature, kUsageCreateNewEffectReport);
    block_load_id_ = reportWithCollection(desc, ReportType::Feature, kUsageBlockLoadReport);
    state_report_id_ = reportWithCollection(desc, ReportType::Input, kUsagePidStateReport);

    int id = -1;
    device_control_ = findArrayInCollection(desc, ReportType::Output, kUsageDeviceControl, id);
    if (device_control_ == nullptr) {
        // Some devices expose Device Control as a bitmask of variable fields.
        const ReportField* f = desc.find(ReportType::Output, kPagePid, kUsageDcEnableActuators);
        if (f != nullptr) {
            device_control_ = f;
            id = f->report_id;
        }
    }
    device_control_id_ = id;
    if (device_gain_id_ >= 0) {
        device_gain_ = out(kUsageDeviceGain, device_gain_id_);
    }

    if (set_effect_id_ < 0) {
        setReason(reason, reason_len, "no Set Effect output report");
        return false;
    }
    if (effect_operation_id_ < 0) {
        setReason(reason, reason_len, "no Effect Operation output report");
        return false;
    }
    if (create_new_effect_id_ < 0 || block_load_id_ < 0) {
        setReason(reason, reason_len, "no Create New Effect / Block Load feature reports");
        return false;
    }
    if (device_control_ == nullptr) {
        setReason(reason, reason_len, "no Device Control report");
        return false;
    }

    int tmp = -1;
    set_effect_type_ = findArrayInCollection(desc, ReportType::Output, kUsageEffectType, tmp);
    if (set_effect_type_ == nullptr || tmp != set_effect_id_) {
        setReason(reason, reason_len, "no Effect Type list in Set Effect");
        return false;
    }
    create_effect_type_ = findArrayInCollection(desc, ReportType::Feature, kUsageEffectType, tmp);
    if (create_effect_type_ == nullptr || tmp != create_new_effect_id_) {
        setReason(reason, reason_len, "no Effect Type list in Create New Effect");
        return false;
    }
    block_load_status_ = findArrayInCollection(desc, ReportType::Feature, kUsageBlockLoadStatus, tmp);
    block_load_index_ = feature(kUsageEffectBlockIndex, block_load_id_);
    if (block_load_status_ == nullptr || block_load_index_ == nullptr || tmp != block_load_id_) {
        setReason(reason, reason_len, "incomplete Block Load report");
        return false;
    }
    operation_ = findArrayInCollection(desc, ReportType::Output, kUsageEffectOperation, tmp);
    if (operation_ == nullptr || tmp != effect_operation_id_) {
        setReason(reason, reason_len, "no Effect Operation list");
        return false;
    }

    block_index_min_ = block_load_index_->logical_min;
    block_index_max_ = block_load_index_->logical_max;
    configured_ = true;
    setReason(reason, reason_len, "ok");
    return true;
}

bool Device::supportsEffect(uint16_t effect_type_usage) const {
    return configured_ && desc_->arrayValueFor(*create_effect_type_, effect_type_usage) >= 0;
}

std::size_t Device::blockLoadReportLen() const {
    if (!configured_) {
        return 0;
    }
    return desc_->reportBytes(ReportType::Feature, static_cast<uint8_t>(block_load_id_)) +
           (desc_->uses_report_ids ? 1 : 0);
}

bool Device::begin(ReportType type, int report_id, Report& out) const {
    out = Report{};
    if (!configured_ || report_id < 0) {
        return false;
    }
    const std::size_t payload = desc_->reportBytes(type, static_cast<uint8_t>(report_id));
    if (payload == 0 || payload + 1 > kMaxReportBytes) {
        return false;
    }
    out.id = static_cast<uint8_t>(report_id);
    if (desc_->uses_report_ids) {
        out.data[0] = out.id;
        out.len = payload + 1;
    } else {
        out.len = payload;
    }
    return true;
}

bool Device::put(Report& r, const ReportField* f, int32_t value, std::size_t element) {
    if (f == nullptr) {
        return false;
    }
    const std::size_t head = (r.data[0] == r.id && r.id != 0) ? 1 : 0;
    return bits::write(r.data + head, r.len - head, *f, element, value);
}

bool Device::putOptional(Report& r, const ReportField* f, int32_t value) {
    if (f == nullptr) {
        return true;  // field not present on this device: nothing to write
    }
    return put(r, f, value);
}

int32_t Device::timeValue(const ReportField& f, uint32_t ms) {
    // Unit 0x1003 = seconds; exponent -3 means the field counts milliseconds.
    int exp = f.unit == 0 ? -3 : f.unit_exponent;
    int64_t v = ms;
    while (exp < -3) {
        v *= 10;
        ++exp;
    }
    while (exp > -3) {
        v /= 10;
        --exp;
    }
    if (v > f.logical_max) {
        v = f.logical_max;
    }
    if (v < f.logical_min) {
        v = f.logical_min;
    }
    return static_cast<int32_t>(v);
}

bool Device::buildDeviceControl(uint16_t dc_usage, Report& r) const {
    if (!begin(ReportType::Output, device_control_id_, r)) {
        return false;
    }
    if (device_control_->is_array) {
        const int32_t v = desc_->arrayValueFor(*device_control_, dc_usage);
        return v >= 0 && put(r, device_control_, v);
    }
    const ReportField* bit = desc_->find(ReportType::Output, kPagePid, dc_usage, device_control_id_);
    return bit != nullptr && put(r, bit, 1);
}

bool Device::buildDeviceGain(float gain, Report& r) const {
    if (device_gain_ == nullptr || !begin(ReportType::Output, device_gain_id_, r)) {
        return false;
    }
    return put(r, device_gain_, bits::scale(*device_gain_, gain));
}

bool Device::buildCreateNewEffect(uint16_t effect_type_usage, Report& r) const {
    if (!begin(ReportType::Feature, create_new_effect_id_, r)) {
        return false;
    }
    const int32_t v = desc_->arrayValueFor(*create_effect_type_, effect_type_usage);
    if (v < 0) {
        return false;
    }
    // "Byte Count" (Generic Desktop 0x3B) is only used by custom force effects: leave 0.
    return put(r, create_effect_type_, v);
}

bool Device::parseBlockLoad(const Report& in, int& block_index, uint16_t& status_usage, uint32_t& ram_available) const {
    if (!configured_) {
        return false;
    }
    const std::size_t head = desc_->uses_report_ids ? 1 : 0;
    if (in.len <= head) {
        return false;
    }
    if (head == 1 && in.data[0] != static_cast<uint8_t>(block_load_id_)) {
        return false;
    }
    bool ok = false;
    block_index = bits::read(in.data + head, in.len - head, *block_load_index_, 0, ok);
    if (!ok) {
        return false;
    }
    const int32_t status = bits::read(in.data + head, in.len - head, *block_load_status_, 0, ok);
    if (!ok) {
        return false;
    }
    status_usage = usageForArrayValue(*desc_, *block_load_status_, status);
    ram_available = 0;
    const ReportField* ram = feature(kUsageRamPoolAvailable, block_load_id_);
    if (ram != nullptr) {
        const int32_t v = bits::read(in.data + head, in.len - head, *ram, 0, ok);
        if (ok && v >= 0) {
            ram_available = static_cast<uint32_t>(v);
        }
    }
    return true;
}

bool Device::buildSetEffect(int block_index, const EffectParams& p, Report& r) const {
    if (!begin(ReportType::Output, set_effect_id_, r)) {
        return false;
    }
    const int id = set_effect_id_;
    const int32_t type_value = desc_->arrayValueFor(*set_effect_type_, p.type);
    if (type_value < 0) {
        return false;
    }
    bool ok = put(r, out(kUsageEffectBlockIndex, id), block_index);
    ok = ok && put(r, set_effect_type_, type_value);

    const ReportField* duration = out(kUsageDuration, id);
    if (duration != nullptr) {
        // PID: an infinite duration is the field's maximum representable value.
        ok =
            ok && put(r, duration, p.duration_ms == 0 ? bits::allOnes(*duration) : timeValue(*duration, p.duration_ms));
    }
    ok = ok && putOptional(r, out(kUsageTriggerRepeatInterval, id), 0);
    ok = ok && putOptional(r, out(kUsageSamplePeriod, id), 0);
    const ReportField* gain = out(kUsageGain, id);
    if (gain != nullptr) {
        ok = ok && put(r, gain, bits::scale(*gain, p.gain));
    }
    ok = ok && putOptional(r, out(kUsageTriggerButton, id), 0);  // out of range = no trigger
    ok = ok && putOptional(r, outInCollection(kPageGenericDesktop, kUsageX, id, kUsageAxesEnable), p.axis_x ? 1 : 0);
    ok = ok && putOptional(r, outInCollection(kPageGenericDesktop, kUsageY, id, kUsageAxesEnable), p.axis_y ? 1 : 0);
    ok = ok && putOptional(r, out(kUsageDirectionEnable, id), p.direction_enable ? 1 : 0);
    const ReportField* dir1 = outInCollection(kPageOrdinal, 1, id, kUsageDirection);
    if (dir1 == nullptr) {
        dir1 = out(kUsageDirection, id);  // single Direction field without ordinals
    }
    if (dir1 != nullptr) {
        ok = ok && put(r, dir1, bits::scale(*dir1, p.direction));
    }
    ok = ok && putOptional(r, outInCollection(kPageOrdinal, 2, id, kUsageDirection), 0);
    const ReportField* delay = out(kUsageStartDelay, id);
    if (delay != nullptr) {
        ok = ok && put(r, delay, timeValue(*delay, p.start_delay_ms));
    }
    return ok;
}

bool Device::buildSetCondition(int block_index, int axis, const Condition& c, Report& r) const {
    if (!begin(ReportType::Output, set_condition_id_, r)) {
        return false;
    }
    const int id = set_condition_id_;
    bool ok = put(r, out(kUsageEffectBlockIndex, id), block_index);
    ok = ok && putOptional(r, out(kUsageParameterBlockOffset, id), axis);
    const ReportField* f;
    if ((f = out(kUsageCpOffset, id)) != nullptr) {
        ok = ok && put(r, f, bits::scale(*f, c.center));
    }
    if ((f = out(kUsagePositiveCoefficient, id)) != nullptr) {
        ok = ok && put(r, f, bits::scale(*f, c.positive_coeff));
    }
    if ((f = out(kUsageNegativeCoefficient, id)) != nullptr) {
        ok = ok && put(r, f, bits::scale(*f, c.negative_coeff));
    }
    if ((f = out(kUsagePositiveSaturation, id)) != nullptr) {
        ok = ok && put(r, f, bits::scale(*f, c.positive_sat));
    }
    if ((f = out(kUsageNegativeSaturation, id)) != nullptr) {
        ok = ok && put(r, f, bits::scale(*f, c.negative_sat));
    }
    if ((f = out(kUsageDeadBand, id)) != nullptr) {
        ok = ok && put(r, f, bits::scale(*f, c.dead_band));
    }
    return ok;
}

bool Device::buildSetPeriodic(int block_index, const Periodic& p, Report& r) const {
    if (!begin(ReportType::Output, set_periodic_id_, r)) {
        return false;
    }
    const int id = set_periodic_id_;
    bool ok = put(r, out(kUsageEffectBlockIndex, id), block_index);
    const ReportField* f;
    if ((f = out(kUsageMagnitude, id)) != nullptr) {
        ok = ok && put(r, f, bits::scale(*f, p.magnitude));
    }
    if ((f = out(kUsageOffset, id)) != nullptr) {
        ok = ok && put(r, f, bits::scale(*f, p.offset));
    }
    if ((f = out(kUsagePhase, id)) != nullptr) {
        ok = ok && put(r, f, bits::scale(*f, p.phase));
    }
    if ((f = out(kUsagePeriod, id)) != nullptr) {
        ok = ok && put(r, f, timeValue(*f, p.period_ms));
    }
    return ok;
}

bool Device::buildEffectOperation(int block_index, uint16_t op_usage, int loop_count, Report& r) const {
    if (!begin(ReportType::Output, effect_operation_id_, r)) {
        return false;
    }
    const int32_t op = desc_->arrayValueFor(*operation_, op_usage);
    if (op < 0) {
        return false;
    }
    bool ok = put(r, out(kUsageEffectBlockIndex, effect_operation_id_), block_index);
    ok = ok && put(r, operation_, op);
    const ReportField* loops = out(kUsageLoopCount, effect_operation_id_);
    if (loops != nullptr) {
        ok = ok && put(r, loops, bits::clamp(*loops, loop_count));
    }
    return ok;
}

bool Device::buildBlockFree(int block_index, Report& r) const {
    if (!begin(ReportType::Output, block_free_id_, r)) {
        return false;
    }
    return put(r, out(kUsageEffectBlockIndex, block_free_id_), block_index);
}

bool Device::decodeState(const uint8_t* report, std::size_t len, State& s) const {
    if (!configured_ || state_report_id_ < 0 || report == nullptr || len == 0) {
        return false;
    }
    const std::size_t head = desc_->uses_report_ids ? 1 : 0;
    if (head == 1 && report[0] != static_cast<uint8_t>(state_report_id_)) {
        return false;
    }
    const uint8_t* payload = report + head;
    const std::size_t plen = len - head;
    auto flag = [&](uint16_t usage, bool& out) {
        const ReportField* f = input(usage, state_report_id_);
        bool ok = false;
        if (f != nullptr) {
            out = bits::read(payload, plen, *f, 0, ok) != 0;
        }
    };
    s = State{};
    flag(kUsageDevicePaused, s.device_paused);
    flag(kUsageActuatorsEnabled, s.actuators_enabled);
    flag(kUsageSafetySwitch, s.safety_switch);
    flag(kUsageActuatorOverrideSwitch, s.actuator_override);
    flag(kUsageActuatorPower, s.actuator_power);
    flag(kUsageEffectPlaying, s.effect_playing);
    const ReportField* idx = input(kUsageEffectBlockIndex, state_report_id_);
    if (idx != nullptr) {
        bool ok = false;
        const int32_t v = bits::read(payload, plen, *idx, 0, ok);
        s.effect_block_index = ok ? v : 0;
    }
    return true;
}

// -------------------------------------------------------------------------------- Controller

bool Controller::deviceControl(uint16_t dc_usage) {
    Report r;
    return device_.buildDeviceControl(dc_usage, r) && transport_.sendOutput(r);
}

bool Controller::setDeviceGain(float gain) {
    Report r;
    if (!device_.hasDeviceGain()) {
        return true;
    }
    return device_.buildDeviceGain(gain, r) && transport_.sendOutput(r);
}

bool Controller::initialise() {
    // Same recipe as the Linux pidff driver: reset twice (the first one is sometimes lost),
    // stop everything, enable the actuators, then full device gain.
    bool ok = deviceControl(kUsageDcDeviceReset);
    transport_.delayMs(kResetSettleMs);
    ok = deviceControl(kUsageDcDeviceReset) && ok;
    transport_.delayMs(kResetSettleMs);
    ok = deviceControl(kUsageDcStopAllEffects) && ok;
    ok = deviceControl(kUsageDcEnableActuators) && ok;
    ok = setDeviceGain(1.0f) && ok;
    return ok;
}

int Controller::createEffect(uint16_t effect_type_usage, uint16_t* status_out) {
    Report r;
    if (!device_.buildCreateNewEffect(effect_type_usage, r) || !transport_.setFeature(r)) {
        return -1;
    }
    const std::size_t len = device_.blockLoadReportLen();
    for (std::size_t i = 0; i < kBlockLoadPolls; ++i) {
        transport_.delayMs(kBlockLoadPollDelayMs);
        Report in;
        if (!transport_.getFeature(device_.blockLoadReportId(), len, in)) {
            continue;
        }
        int index = 0;
        uint16_t status = 0;
        uint32_t ram = 0;
        if (!device_.parseBlockLoad(in, index, status, ram)) {
            continue;
        }
        if (status_out != nullptr) {
            *status_out = status;
        }
        if (status == kUsageBlockLoadSuccess) {
            return index;
        }
        if (status == kUsageBlockLoadFull || status == kUsageBlockLoadError) {
            return -1;
        }
    }
    return -1;
}

bool Controller::freeEffect(int block_index) {
    Report r;
    return device_.buildBlockFree(block_index, r) && transport_.sendOutput(r);
}

bool Controller::startEffect(int block_index, int loop_count) {
    Report r;
    return device_.buildEffectOperation(block_index, kUsageOpEffectStart, loop_count, r) && transport_.sendOutput(r);
}

bool Controller::stopEffect(int block_index) {
    Report r;
    return device_.buildEffectOperation(block_index, kUsageOpEffectStop, 0, r) && transport_.sendOutput(r);
}

bool Controller::uploadCondition(int block_index, uint16_t effect_type_usage, const EffectParams& e,
                                 const Condition& c) {
    if (!device_.hasConditionReport()) {
        return false;
    }
    EffectParams params = e;
    params.type = effect_type_usage;
    Report r;
    if (!device_.buildSetEffect(block_index, params, r) || !transport_.sendOutput(r)) {
        return false;
    }
    for (int axis = 0; axis < 2; ++axis) {
        if (!device_.buildSetCondition(block_index, axis, c, r) || !transport_.sendOutput(r)) {
            return false;
        }
    }
    return true;
}

bool Controller::uploadPeriodic(int block_index, uint16_t effect_type_usage, const EffectParams& e, const Periodic& p) {
    if (!device_.hasPeriodicReport()) {
        return false;
    }
    EffectParams params = e;
    params.type = effect_type_usage;
    Report r;
    if (!device_.buildSetEffect(block_index, params, r) || !transport_.sendOutput(r)) {
        return false;
    }
    return device_.buildSetPeriodic(block_index, p, r) && transport_.sendOutput(r);
}

}  // namespace hid::pid
