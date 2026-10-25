// SPDX-License-Identifier: MIT
// Unit tests for the PID force-feedback client against the SideWinder Force Feedback 2.
#include <unity.h>

#include <cstring>
#include <vector>

#include "fixtures/sidewinder_ffb2_descriptor.hpp"
#include "hid/bitfield.hpp"
#include "hid/pid.hpp"
#include "hid/report_descriptor.hpp"

using namespace hid;
using namespace hid::pid;

void setUp() {}
void tearDown() {}

namespace {

ParsedDescriptor g_desc;

struct Sent {
    char kind;  // 'o' output, 'f' set feature
    std::vector<uint8_t> bytes;
};

/// Fake transport recording every report and answering Block Load queries.
class FakeTransport : public Transport {
public:
    std::vector<Sent> sent;
    std::vector<uint8_t> block_load_answer;  // full report (with ID)
    int feature_reads = 0;
    uint32_t delayed_ms = 0;

    bool sendOutput(const Report& r) override {
        sent.push_back({'o', std::vector<uint8_t>(r.data, r.data + r.len)});
        return true;
    }
    bool setFeature(const Report& r) override {
        sent.push_back({'f', std::vector<uint8_t>(r.data, r.data + r.len)});
        return true;
    }
    bool getFeature(uint8_t report_id, std::size_t expected_len, Report& out) override {
        ++feature_reads;
        if (block_load_answer.empty() || block_load_answer[0] != report_id) {
            return false;
        }
        out = Report{};
        out.id = report_id;
        out.len = block_load_answer.size() < expected_len ? block_load_answer.size() : expected_len;
        std::memcpy(out.data, block_load_answer.data(), out.len);
        return true;
    }
    void delayMs(uint32_t ms) override { delayed_ms += ms; }
};

const ParsedDescriptor& ffb2() {
    static bool parsed = false;
    if (!parsed) {
        TEST_ASSERT_EQUAL(ParseResult::Ok,
                          parse(fixtures::kSideWinderFfb2Descriptor, fixtures::kSideWinderFfb2DescriptorLen, g_desc));
        parsed = true;
    }
    return g_desc;
}

Device configuredDevice() {
    Device dev;
    char why[64];
    TEST_ASSERT_TRUE_MESSAGE(dev.configure(ffb2(), why, sizeof(why)), why);
    return dev;
}

int32_t readField(const std::vector<uint8_t>& report, ReportType type, uint16_t page, uint16_t usage, int report_id,
                  std::size_t nth = 0) {
    const ReportField* f = ffb2().find(type, page, usage, report_id, nth);
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "field not found");
    bool ok = false;
    const int32_t v = bits::read(report.data() + 1, report.size() - 1, *f, 0, ok);
    TEST_ASSERT_TRUE(ok);
    return v;
}

}  // namespace

// --------------------------------------------------------------------- descriptor parsing

static void test_ffb2_output_and_feature_layout() {
    const ParsedDescriptor& d = ffb2();
    TEST_ASSERT_EQUAL_UINT32(63, d.out_field_count);
    TEST_ASSERT_EQUAL_UINT32(9, d.feature_field_count);
    TEST_ASSERT_EQUAL_UINT32(15, d.reportBytes(ReportType::Output, 1));  // Set Effect
    TEST_ASSERT_EQUAL_UINT32(8, d.reportBytes(ReportType::Output, 3));   // Set Condition
    TEST_ASSERT_EQUAL_UINT32(6, d.reportBytes(ReportType::Output, 4));   // Set Periodic
    TEST_ASSERT_EQUAL_UINT32(3, d.reportBytes(ReportType::Output, 10));  // Effect Operation
    TEST_ASSERT_EQUAL_UINT32(1, d.reportBytes(ReportType::Output, 12));  // Device Control
    TEST_ASSERT_EQUAL_UINT32(1, d.reportBytes(ReportType::Output, 13));  // Device Gain
    TEST_ASSERT_EQUAL_UINT32(3, d.reportBytes(ReportType::Feature, 1));  // Create New Effect
    TEST_ASSERT_EQUAL_UINT32(4, d.reportBytes(ReportType::Feature, 2));  // Block Load
    TEST_ASSERT_EQUAL_UINT32(4, d.reportBytes(ReportType::Feature, 3));  // PID Pool
    TEST_ASSERT_EQUAL_UINT32(2, d.reportBytes(ReportType::Input, 2));    // PID state

    // Duration is in milliseconds (seconds, exponent -3), 16 bits, 0..32767.
    const ReportField* duration = d.find(ReportType::Output, kPagePid, kUsageDuration, 1);
    TEST_ASSERT_NOT_NULL(duration);
    TEST_ASSERT_EQUAL_HEX32(0x1003, duration->unit);
    TEST_ASSERT_EQUAL_INT8(-3, duration->unit_exponent);
    TEST_ASSERT_EQUAL_UINT8(16, duration->bit_size);
    TEST_ASSERT_EQUAL_UINT16(16, duration->bit_offset);

    // Effect Type is an array with an explicit usage list: spring is the 8th entry.
    const ReportField* type = nullptr;
    for (std::size_t i = 0; i < d.out_field_count; ++i) {
        if (d.out_fields[i].report_id == 1 && d.out_fields[i].is_array) {
            type = &d.out_fields[i];
        }
    }
    TEST_ASSERT_NOT_NULL(type);
    TEST_ASSERT_EQUAL_UINT16(kUsageEffectType, type->collection_usage);
    TEST_ASSERT_EQUAL_UINT8(12, type->usage_list_count);
    TEST_ASSERT_EQUAL_INT32(8, d.arrayValueFor(*type, kUsageEtSpring));
    TEST_ASSERT_EQUAL_INT32(4, d.arrayValueFor(*type, kUsageEtSine));
    TEST_ASSERT_EQUAL_INT32(1, d.arrayValueFor(*type, kUsageEtConstantForce));
    TEST_ASSERT_EQUAL_INT32(-1, d.arrayValueFor(*type, 0x99));

    // Axes Enable X/Y live in their own collection at bits 80/81.
    const ReportField* ax = d.find(ReportType::Output, kPageGenericDesktop, kUsageX, 1);
    TEST_ASSERT_NOT_NULL(ax);
    TEST_ASSERT_EQUAL_UINT16(kUsageAxesEnable, ax->collection_usage);
    TEST_ASSERT_EQUAL_UINT16(80, ax->bit_offset);
}

static void test_bitfield_write_and_read_roundtrip() {
    ReportField f;
    f.bit_offset = 5;
    f.bit_size = 10;
    f.logical_min = -512;
    f.logical_max = 511;
    uint8_t buf[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_TRUE(bits::write(buf, sizeof(buf), f, 0, -300));
    bool ok = false;
    TEST_ASSERT_EQUAL_INT32(-300, bits::read(buf, sizeof(buf), f, 0, ok));
    TEST_ASSERT_TRUE(ok);
    // Bits outside the field are untouched.
    TEST_ASSERT_EQUAL_HEX8(0x1F, buf[0] & 0x1F);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[3]);
    TEST_ASSERT_FALSE(bits::write(buf, 1, f, 0, 1));  // does not fit

    ReportField g;
    g.bit_size = 8;
    g.logical_min = 0;
    g.logical_max = 255;
    TEST_ASSERT_EQUAL_INT32(255, bits::scale(g, 1.0f));
    TEST_ASSERT_EQUAL_INT32(0, bits::scale(g, -2.0f));
    TEST_ASSERT_EQUAL_INT32(128, bits::scale(g, 0.5f));
    TEST_ASSERT_EQUAL_INT32(255, bits::allOnes(g));
    ReportField s;
    s.bit_size = 8;
    s.logical_min = -128;
    s.logical_max = 127;
    TEST_ASSERT_EQUAL_INT32(127, bits::scale(s, 1.0f));
    TEST_ASSERT_EQUAL_INT32(-128, bits::scale(s, -1.0f));
    TEST_ASSERT_EQUAL_INT32(0, bits::scale(s, 0.0f));
    TEST_ASSERT_EQUAL_INT32(64, bits::scale(s, 0.5f));
}

static void test_time_units() {
    ReportField ms;
    ms.unit = 0x1003;
    ms.unit_exponent = -3;
    ms.logical_min = 0;
    ms.logical_max = 32767;
    TEST_ASSERT_EQUAL_INT32(600, Device::timeValue(ms, 600));
    TEST_ASSERT_EQUAL_INT32(32767, Device::timeValue(ms, 60000));  // clamped
    ReportField tenth;
    tenth = ms;
    tenth.unit_exponent = -4;
    TEST_ASSERT_EQUAL_INT32(6000, Device::timeValue(tenth, 600));
    ReportField secs;
    secs = ms;
    secs.unit_exponent = 0;
    TEST_ASSERT_EQUAL_INT32(2, Device::timeValue(secs, 2500));
    ReportField none;
    none.logical_max = 1000;
    TEST_ASSERT_EQUAL_INT32(250, Device::timeValue(none, 250));
}

// --------------------------------------------------------------------- device configuration

static void test_device_configures_from_ffb2() {
    Device dev = configuredDevice();
    TEST_ASSERT_TRUE(dev.configured());
    TEST_ASSERT_TRUE(dev.supportsEffect(kUsageEtSpring));
    TEST_ASSERT_TRUE(dev.supportsEffect(kUsageEtSine));
    TEST_ASSERT_TRUE(dev.supportsEffect(kUsageEtDamper));
    TEST_ASSERT_FALSE(dev.supportsEffect(0x7F));
    TEST_ASSERT_TRUE(dev.hasConditionReport());
    TEST_ASSERT_TRUE(dev.hasPeriodicReport());
    TEST_ASSERT_TRUE(dev.hasDeviceGain());
    TEST_ASSERT_TRUE(dev.hasStateReport());
    TEST_ASSERT_EQUAL_INT(1, dev.blockIndexMin());
    TEST_ASSERT_EQUAL_INT(40, dev.blockIndexMax());
    TEST_ASSERT_EQUAL_UINT8(2, dev.blockLoadReportId());
    TEST_ASSERT_EQUAL_UINT32(5, dev.blockLoadReportLen());
    TEST_ASSERT_EQUAL_UINT8(2, dev.stateReportId());
}

static void test_device_rejects_non_pid_descriptor() {
    const uint8_t gamepad[] = {0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x15, 0x00, 0x26, 0xFF, 0x00,
                               0x75, 0x08, 0x95, 0x02, 0x09, 0x30, 0x09, 0x31, 0x81, 0x02, 0xC0};
    ParsedDescriptor d;
    TEST_ASSERT_EQUAL(ParseResult::Ok, parse(gamepad, sizeof(gamepad), d));
    Device dev;
    char why[64] = {0};
    TEST_ASSERT_FALSE(dev.configure(d, why, sizeof(why)));
    TEST_ASSERT_TRUE(std::strlen(why) > 0);
    Report r;
    TEST_ASSERT_FALSE(dev.buildDeviceGain(1.0f, r));
}

// --------------------------------------------------------------------- report builders

static void test_device_control_and_gain_reports() {
    Device dev = configuredDevice();
    Report r;
    TEST_ASSERT_TRUE(dev.buildDeviceControl(kUsageDcDeviceReset, r));
    TEST_ASSERT_EQUAL_UINT32(2, r.len);
    TEST_ASSERT_EQUAL_HEX8(12, r.data[0]);
    TEST_ASSERT_EQUAL_HEX8(4, r.data[1]);  // reset is the 4th entry of the list
    TEST_ASSERT_TRUE(dev.buildDeviceControl(kUsageDcEnableActuators, r));
    TEST_ASSERT_EQUAL_HEX8(1, r.data[1]);
    TEST_ASSERT_TRUE(dev.buildDeviceControl(kUsageDcStopAllEffects, r));
    TEST_ASSERT_EQUAL_HEX8(3, r.data[1]);
    TEST_ASSERT_FALSE(dev.buildDeviceControl(0x11, r));

    TEST_ASSERT_TRUE(dev.buildDeviceGain(1.0f, r));
    TEST_ASSERT_EQUAL_UINT32(2, r.len);
    TEST_ASSERT_EQUAL_HEX8(13, r.data[0]);
    TEST_ASSERT_EQUAL_HEX8(255, r.data[1]);
    TEST_ASSERT_TRUE(dev.buildDeviceGain(0.5f, r));
    TEST_ASSERT_EQUAL_HEX8(128, r.data[1]);
}

static void test_create_new_effect_and_block_load() {
    Device dev = configuredDevice();
    Report r;
    TEST_ASSERT_TRUE(dev.buildCreateNewEffect(kUsageEtSpring, r));
    TEST_ASSERT_EQUAL_UINT32(4, r.len);
    TEST_ASSERT_EQUAL_HEX8(1, r.data[0]);
    TEST_ASSERT_EQUAL_HEX8(8, r.data[1]);  // spring
    TEST_ASSERT_EQUAL_HEX8(0, r.data[2]);  // byte count
    TEST_ASSERT_EQUAL_HEX8(0, r.data[3]);
    TEST_ASSERT_FALSE(dev.buildCreateNewEffect(0x60, r));

    // Block Load answer: id 2, block 7, status 1 (success), RAM available 0x1234.
    Report in;
    in.id = 2;
    in.len = 5;
    const uint8_t answer[5] = {0x02, 7, 1, 0x34, 0x12};
    std::memcpy(in.data, answer, sizeof(answer));
    int index = 0;
    uint16_t status = 0;
    uint32_t ram = 0;
    TEST_ASSERT_TRUE(dev.parseBlockLoad(in, index, status, ram));
    TEST_ASSERT_EQUAL_INT(7, index);
    TEST_ASSERT_EQUAL_HEX16(kUsageBlockLoadSuccess, status);
    TEST_ASSERT_EQUAL_UINT32(0x1234, ram);
    in.data[2] = 2;
    TEST_ASSERT_TRUE(dev.parseBlockLoad(in, index, status, ram));
    TEST_ASSERT_EQUAL_HEX16(kUsageBlockLoadFull, status);
    in.data[0] = 3;  // wrong report
    TEST_ASSERT_FALSE(dev.parseBlockLoad(in, index, status, ram));
}

static void test_set_effect_report_spring_infinite() {
    Device dev = configuredDevice();
    EffectParams e;  // defaults: per-axis form (Axes Enable X+Y, Direction Enable clear)
    e.type = kUsageEtSpring;
    e.duration_ms = 0;
    e.gain = 1.0f;
    Report r;
    TEST_ASSERT_TRUE(dev.buildSetEffect(3, e, r));
    TEST_ASSERT_EQUAL_UINT32(16, r.len);
    TEST_ASSERT_EQUAL_HEX8(1, r.data[0]);
    std::vector<uint8_t> v(r.data, r.data + r.len);
    TEST_ASSERT_EQUAL_INT32(3, readField(v, ReportType::Output, kPagePid, kUsageEffectBlockIndex, 1));
    TEST_ASSERT_EQUAL_HEX8(8, r.data[2]);     // effect type = spring
    TEST_ASSERT_EQUAL_HEX8(0xFF, r.data[3]);  // duration = all ones (infinite)
    TEST_ASSERT_EQUAL_HEX8(0xFF, r.data[4]);
    TEST_ASSERT_EQUAL_INT32(0, readField(v, ReportType::Output, kPagePid, kUsageTriggerRepeatInterval, 1));
    TEST_ASSERT_EQUAL_INT32(0, readField(v, ReportType::Output, kPagePid, kUsageSamplePeriod, 1));
    TEST_ASSERT_EQUAL_INT32(255, readField(v, ReportType::Output, kPagePid, kUsageGain, 1));
    TEST_ASSERT_EQUAL_INT32(0, readField(v, ReportType::Output, kPagePid, kUsageTriggerButton, 1));
    TEST_ASSERT_EQUAL_INT32(1, readField(v, ReportType::Output, kPageGenericDesktop, kUsageX, 1));
    TEST_ASSERT_EQUAL_INT32(1, readField(v, ReportType::Output, kPageGenericDesktop, kUsageY, 1));
    TEST_ASSERT_EQUAL_INT32(0, readField(v, ReportType::Output, kPagePid, kUsageDirectionEnable, 1));
    TEST_ASSERT_EQUAL_INT32(0, readField(v, ReportType::Output, kPageOrdinal, 1, 1));
    TEST_ASSERT_EQUAL_INT32(0, readField(v, ReportType::Output, kPagePid, kUsageStartDelay, 1));
    TEST_ASSERT_EQUAL_HEX8(0x03, r.data[11]);  // payload byte 10: bit0 X, bit1 Y, bit2 direction enable
    const uint8_t expected[16] = {0x01, 0x03, 0x08, 0xFF, 0xFF, 0x00, 0x00, 0x00,
                                  0x00, 0xFF, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, r.data, 16);
}

static void test_set_effect_report_linux_direction_form() {
    // What hid-pidff sends for every effect: Direction Enable set, Axes Enable bits clear,
    // direction 0 (north). Selected with `ffb.direction 1`.
    Device dev = configuredDevice();
    EffectParams e;
    e.type = kUsageEtSpring;
    e.linuxDirectionForm();
    Report r;
    TEST_ASSERT_TRUE(dev.buildSetEffect(3, e, r));
    std::vector<uint8_t> v(r.data, r.data + r.len);
    TEST_ASSERT_EQUAL_INT32(0, readField(v, ReportType::Output, kPageGenericDesktop, kUsageX, 1));
    TEST_ASSERT_EQUAL_INT32(0, readField(v, ReportType::Output, kPageGenericDesktop, kUsageY, 1));
    TEST_ASSERT_EQUAL_INT32(1, readField(v, ReportType::Output, kPagePid, kUsageDirectionEnable, 1));
    TEST_ASSERT_EQUAL_INT32(0, readField(v, ReportType::Output, kPageOrdinal, 1, 1));
    TEST_ASSERT_EQUAL_HEX8(0x04, r.data[11]);
}

static void test_set_effect_report_sine_timed() {
    Device dev = configuredDevice();
    EffectParams e;
    e.type = kUsageEtSine;
    e.duration_ms = 600;
    e.gain = 0.5f;
    e.linuxDirectionForm(0.25f);  // east
    e.start_delay_ms = 100;
    Report r;
    TEST_ASSERT_TRUE(dev.buildSetEffect(1, e, r));
    std::vector<uint8_t> v(r.data, r.data + r.len);
    TEST_ASSERT_EQUAL_HEX8(4, r.data[2]);
    TEST_ASSERT_EQUAL_INT32(600, readField(v, ReportType::Output, kPagePid, kUsageDuration, 1));
    TEST_ASSERT_EQUAL_INT32(128, readField(v, ReportType::Output, kPagePid, kUsageGain, 1));
    TEST_ASSERT_EQUAL_HEX8(0x04, r.data[11]);  // direction enable only
    TEST_ASSERT_EQUAL_INT32(64, readField(v, ReportType::Output, kPageOrdinal, 1, 1));
    TEST_ASSERT_EQUAL_INT32(100, readField(v, ReportType::Output, kPagePid, kUsageStartDelay, 1));
    e.type = 0x29;  // not an effect type
    TEST_ASSERT_FALSE(dev.buildSetEffect(1, e, r));
}

static void test_pool_report() {
    Device dev = configuredDevice();
    TEST_ASSERT_TRUE(dev.hasPoolReport());
    TEST_ASSERT_EQUAL_UINT8(3, dev.poolReportId());
    TEST_ASSERT_EQUAL_UINT32(5, dev.poolReportLen());
    // id 3: RAM pool size (16 bits), simultaneous effects max (8), device managed (bit 24),
    // shared parameter blocks (bit 25).
    Report in;
    const uint8_t managed[5] = {0x03, 0x00, 0x10, 0x0A, 0x03};
    std::memcpy(in.data, managed, 5);
    in.len = 5;
    in.id = 3;
    PoolInfo p;
    TEST_ASSERT_TRUE(dev.parsePool(in, p));
    TEST_ASSERT_EQUAL_UINT32(4096, p.ram_pool_size);
    TEST_ASSERT_EQUAL_UINT32(10, p.simultaneous_max);
    TEST_ASSERT_TRUE(p.has_device_managed_flag);
    TEST_ASSERT_TRUE(p.device_managed);
    TEST_ASSERT_TRUE(p.shared_parameter_blocks);
    in.data[4] = 0x00;
    TEST_ASSERT_TRUE(dev.parsePool(in, p));
    TEST_ASSERT_TRUE(p.has_device_managed_flag);
    TEST_ASSERT_FALSE(p.device_managed);
    in.data[0] = 0x02;  // wrong report
    TEST_ASSERT_FALSE(dev.parsePool(in, p));

    FakeTransport t;
    Controller ctl(dev, t);
    TEST_ASSERT_FALSE(ctl.readPool(p));                    // device does not answer
    t.block_load_answer = {0x03, 0x00, 0x10, 0x0A, 0x03};  // the fake answers any feature id it holds
    TEST_ASSERT_TRUE(ctl.readPool(p));
    TEST_ASSERT_TRUE(p.device_managed);
}

static void test_set_condition_report() {
    Device dev = configuredDevice();
    Condition c;
    c.center = 0.0f;
    c.positive_coeff = 0.75f;
    c.negative_coeff = 0.75f;
    c.positive_sat = 1.0f;
    c.negative_sat = 1.0f;
    c.dead_band = 0.1f;
    Report r;
    TEST_ASSERT_TRUE(dev.buildSetCondition(3, 1, c, r));
    TEST_ASSERT_EQUAL_UINT32(9, r.len);
    TEST_ASSERT_EQUAL_HEX8(3, r.data[0]);
    std::vector<uint8_t> v(r.data, r.data + r.len);
    TEST_ASSERT_EQUAL_INT32(3, readField(v, ReportType::Output, kPagePid, kUsageEffectBlockIndex, 3));
    TEST_ASSERT_EQUAL_INT32(1, readField(v, ReportType::Output, kPagePid, kUsageParameterBlockOffset, 3));
    TEST_ASSERT_EQUAL_INT32(0, readField(v, ReportType::Output, kPagePid, kUsageCpOffset, 3));
    TEST_ASSERT_EQUAL_INT32(95, readField(v, ReportType::Output, kPagePid, kUsagePositiveCoefficient, 3));
    TEST_ASSERT_EQUAL_INT32(95, readField(v, ReportType::Output, kPagePid, kUsageNegativeCoefficient, 3));
    TEST_ASSERT_EQUAL_INT32(255, readField(v, ReportType::Output, kPagePid, kUsagePositiveSaturation, 3));
    TEST_ASSERT_EQUAL_INT32(255, readField(v, ReportType::Output, kPagePid, kUsageNegativeSaturation, 3));
    TEST_ASSERT_EQUAL_INT32(26, readField(v, ReportType::Output, kPagePid, kUsageDeadBand, 3));
    // Raw layout check: index, PBO nibble, cp, +coef, -coef, +sat, -sat, dead band.
    const uint8_t expected[9] = {0x03, 0x03, 0x01, 0x00, 95, 95, 0xFF, 0xFF, 26};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, r.data, 9);
}

static void test_set_periodic_and_operation_reports() {
    Device dev = configuredDevice();
    Periodic p;
    p.magnitude = 1.0f;
    p.offset = 0.0f;
    p.phase = 0.0f;
    p.period_ms = 50;
    Report r;
    TEST_ASSERT_TRUE(dev.buildSetPeriodic(5, p, r));
    const uint8_t periodic[7] = {0x04, 5, 0xFF, 0x00, 0x00, 50, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(periodic, r.data, 7);
    TEST_ASSERT_EQUAL_UINT32(7, r.len);

    TEST_ASSERT_TRUE(dev.buildEffectOperation(5, kUsageOpEffectStart, 1, r));
    const uint8_t start[4] = {0x0A, 5, 1, 1};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(start, r.data, 4);
    TEST_ASSERT_TRUE(dev.buildEffectOperation(5, kUsageOpEffectStop, 0, r));
    const uint8_t stop[4] = {0x0A, 5, 3, 0};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(stop, r.data, 4);
    TEST_ASSERT_TRUE(dev.buildEffectOperation(5, kUsageOpEffectStart, 999, r));
    TEST_ASSERT_EQUAL_HEX8(255, r.data[3]);  // loop count clamped

    TEST_ASSERT_TRUE(dev.buildBlockFree(5, r));
    const uint8_t free_[2] = {0x0B, 5};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(free_, r.data, 2);
}

static void test_decode_state_report() {
    Device dev = configuredDevice();
    // id 2: bit0 paused, bit1 actuators enabled, bit2 safety switch, bit3 override, bit4 power;
    // byte 1: bit0 effect playing, bits1..7 block index.
    const uint8_t report[3] = {0x02, 0x12, static_cast<uint8_t>((9 << 1) | 1)};
    State s;
    TEST_ASSERT_TRUE(dev.decodeState(report, sizeof(report), s));
    TEST_ASSERT_FALSE(s.device_paused);
    TEST_ASSERT_TRUE(s.actuators_enabled);
    TEST_ASSERT_FALSE(s.safety_switch);
    TEST_ASSERT_FALSE(s.actuator_override);
    TEST_ASSERT_TRUE(s.actuator_power);
    TEST_ASSERT_TRUE(s.effect_playing);
    TEST_ASSERT_EQUAL_INT(9, s.effect_block_index);
    const uint8_t joystick[13] = {0x01};
    TEST_ASSERT_FALSE(dev.decodeState(joystick, sizeof(joystick), s));
}

// --------------------------------------------------------------------- controller sequences

static void test_controller_initialise_sequence() {
    Device dev = configuredDevice();
    FakeTransport t;
    Controller ctl(dev, t);
    TEST_ASSERT_TRUE(ctl.initialise());
    TEST_ASSERT_EQUAL_UINT32(5, t.sent.size());
    TEST_ASSERT_EQUAL_HEX8(12, t.sent[0].bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(4, t.sent[0].bytes[1]);  // reset
    TEST_ASSERT_EQUAL_HEX8(4, t.sent[1].bytes[1]);  // reset again
    TEST_ASSERT_EQUAL_HEX8(3, t.sent[2].bytes[1]);  // stop all
    TEST_ASSERT_EQUAL_HEX8(1, t.sent[3].bytes[1]);  // enable actuators
    TEST_ASSERT_EQUAL_HEX8(13, t.sent[4].bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(255, t.sent[4].bytes[1]);  // full gain
    TEST_ASSERT_TRUE(t.delayed_ms >= 20);
}

static void test_controller_creates_spring_and_starts_it() {
    Device dev = configuredDevice();
    FakeTransport t;
    t.block_load_answer = {0x02, 1, 1, 0x00, 0x00};  // block 1, success
    Controller ctl(dev, t);

    uint16_t status = 0;
    const int block = ctl.createEffect(kUsageEtSpring, &status);
    TEST_ASSERT_EQUAL_INT(1, block);
    TEST_ASSERT_EQUAL_HEX16(kUsageBlockLoadSuccess, status);
    TEST_ASSERT_EQUAL_UINT32(1, t.sent.size());
    TEST_ASSERT_EQUAL_CHAR('f', t.sent[0].kind);
    TEST_ASSERT_EQUAL_HEX8(8, t.sent[0].bytes[1]);
    TEST_ASSERT_EQUAL_INT(1, t.feature_reads);

    EffectParams e;
    Condition c;
    c.positive_coeff = 0.6f;
    c.negative_coeff = 0.6f;
    TEST_ASSERT_TRUE(ctl.uploadCondition(block, kUsageEtSpring, e, c));
    TEST_ASSERT_EQUAL_UINT32(4, t.sent.size());
    TEST_ASSERT_EQUAL_HEX8(1, t.sent[1].bytes[0]);      // set effect
    TEST_ASSERT_EQUAL_HEX8(8, t.sent[1].bytes[2]);      // spring
    TEST_ASSERT_EQUAL_HEX8(0x03, t.sent[1].bytes[11]);  // per-axis form matches the two condition blocks
    TEST_ASSERT_EQUAL_HEX8(3, t.sent[2].bytes[0]);      // condition axis 0
    TEST_ASSERT_EQUAL_HEX8(0, t.sent[2].bytes[2] & 0x0F);
    TEST_ASSERT_EQUAL_HEX8(3, t.sent[3].bytes[0]);  // condition axis 1
    TEST_ASSERT_EQUAL_HEX8(1, t.sent[3].bytes[2] & 0x0F);
    TEST_ASSERT_EQUAL_HEX8(76, t.sent[3].bytes[4]);  // 0.6 * 127

    TEST_ASSERT_TRUE(ctl.startEffect(block));
    TEST_ASSERT_EQUAL_HEX8(0x0A, t.sent[4].bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(1, t.sent[4].bytes[2]);
    TEST_ASSERT_TRUE(ctl.stopEffect(block));
    TEST_ASSERT_EQUAL_HEX8(3, t.sent[5].bytes[2]);
    TEST_ASSERT_TRUE(ctl.freeEffect(block));
    TEST_ASSERT_EQUAL_HEX8(0x0B, t.sent[6].bytes[0]);
}

static void test_controller_handles_block_load_failures() {
    Device dev = configuredDevice();
    FakeTransport t;
    Controller ctl(dev, t);
    // Device never answers: gives up after the polls.
    TEST_ASSERT_EQUAL_INT(-1, ctl.createEffect(kUsageEtSine));
    TEST_ASSERT_TRUE(t.feature_reads >= 10);
    // Device says "full".
    t.block_load_answer = {0x02, 0, 2, 0, 0};
    uint16_t status = 0;
    TEST_ASSERT_EQUAL_INT(-1, ctl.createEffect(kUsageEtSine, &status));
    TEST_ASSERT_EQUAL_HEX16(kUsageBlockLoadFull, status);
    // Unsupported effect type never reaches the device.
    const std::size_t before = t.sent.size();
    TEST_ASSERT_EQUAL_INT(-1, ctl.createEffect(0x50));
    TEST_ASSERT_EQUAL_UINT32(before, t.sent.size());
}

static void test_controller_periodic_upload() {
    Device dev = configuredDevice();
    FakeTransport t;
    Controller ctl(dev, t);
    EffectParams e;
    e.duration_ms = 600;
    Periodic p;
    p.period_ms = 40;
    TEST_ASSERT_TRUE(ctl.uploadPeriodic(2, kUsageEtSine, e, p));
    TEST_ASSERT_EQUAL_UINT32(2, t.sent.size());
    TEST_ASSERT_EQUAL_HEX8(1, t.sent[0].bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(4, t.sent[0].bytes[2]);  // sine
    TEST_ASSERT_EQUAL_HEX8(0x04, t.sent[1].bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(40, t.sent[1].bytes[5]);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_ffb2_output_and_feature_layout);
    RUN_TEST(test_bitfield_write_and_read_roundtrip);
    RUN_TEST(test_time_units);
    RUN_TEST(test_device_configures_from_ffb2);
    RUN_TEST(test_device_rejects_non_pid_descriptor);
    RUN_TEST(test_device_control_and_gain_reports);
    RUN_TEST(test_create_new_effect_and_block_load);
    RUN_TEST(test_set_effect_report_spring_infinite);
    RUN_TEST(test_set_effect_report_linux_direction_form);
    RUN_TEST(test_set_effect_report_sine_timed);
    RUN_TEST(test_pool_report);
    RUN_TEST(test_set_condition_report);
    RUN_TEST(test_set_periodic_and_operation_reports);
    RUN_TEST(test_decode_state_report);
    RUN_TEST(test_controller_initialise_sequence);
    RUN_TEST(test_controller_creates_spring_and_starts_it);
    RUN_TEST(test_controller_handles_block_load_failures);
    RUN_TEST(test_controller_periodic_upload);
    return UNITY_END();
}
