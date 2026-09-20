// SPDX-License-Identifier: MIT
// Unit tests for the joystick -> RC channel mapper.
#include <unity.h>

#include "rc/config.hpp"
#include "rc/mapping.hpp"

using namespace rc;

void setUp() {}
void tearDown() {}

static ChannelConfig axisCh(Axis axis) {
    ChannelConfig c;
    c.source = SourceType::Axis;
    c.index = static_cast<uint8_t>(axis);
    return c;
}

static void test_axis_linear_scaling() {
    MappingConfig cfg;
    cfg.ch[0] = axisCh(Axis::X);
    Mapper m;
    InputState in;

    in.axis[0] = 0.0f;
    TEST_ASSERT_EQUAL_UINT16(1500, m.map(in, cfg).us[0]);
    in.axis[0] = 1.0f;
    TEST_ASSERT_EQUAL_UINT16(2000, m.map(in, cfg).us[0]);
    in.axis[0] = -1.0f;
    TEST_ASSERT_EQUAL_UINT16(1000, m.map(in, cfg).us[0]);
    in.axis[0] = 0.5f;
    TEST_ASSERT_EQUAL_UINT16(1750, m.map(in, cfg).us[0]);
    in.axis[0] = 3.0f;  // out of range input is clamped
    TEST_ASSERT_EQUAL_UINT16(2000, m.map(in, cfg).us[0]);
}

static void test_axis_invert_endpoints_and_trim() {
    MappingConfig cfg;
    cfg.ch[0] = axisCh(Axis::Y);
    cfg.ch[0].invert = true;
    cfg.ch[0].min_us = 1100;
    cfg.ch[0].max_us = 1900;
    cfg.ch[0].trim_us = 20;
    Mapper m;
    InputState in;
    in.axis[static_cast<int>(Axis::Y)] = 1.0f;
    TEST_ASSERT_EQUAL_UINT16(1120, m.map(in, cfg).us[0]);  // inverted -> min, plus trim
    in.axis[static_cast<int>(Axis::Y)] = -1.0f;
    TEST_ASSERT_EQUAL_UINT16(1900, m.map(in, cfg).us[0]);  // trim cannot exceed max
    in.axis[static_cast<int>(Axis::Y)] = 0.0f;
    TEST_ASSERT_EQUAL_UINT16(1520, m.map(in, cfg).us[0]);
}

static void test_deadband_and_expo_shaping() {
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, shapeAxis(0.04f, 5, 0));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, shapeAxis(-0.05f, 5, 0));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, shapeAxis(1.0f, 5, 0));  // full travel preserved
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -1.0f, shapeAxis(-1.0f, 5, 0));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, shapeAxis(0.525f, 5, 0));  // rescaled linearly
    // Expo keeps the end points and is softer around the centre.
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, shapeAxis(1.0f, 0, 50));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -1.0f, shapeAxis(-1.0f, 0, 50));
    TEST_ASSERT_TRUE(shapeAxis(0.5f, 0, 50) < 0.5f);
    TEST_ASSERT_TRUE(shapeAxis(0.5f, 0, 50) > 0.0f);
    // Monotonic
    float last = -1.0f;
    for (int i = -100; i <= 100; ++i) {
        const float v = shapeAxis(static_cast<float>(i) / 100.0f, 10, 30);
        TEST_ASSERT_TRUE(v >= last);
        last = v;
    }
}

static void test_button_momentary() {
    MappingConfig cfg;
    cfg.ch[0].source = SourceType::Button;
    cfg.ch[0].index = 2;  // button 3
    Mapper m;
    InputState in;
    TEST_ASSERT_EQUAL_UINT16(1000, m.map(in, cfg).us[0]);
    in.buttons = 1u << 2;
    TEST_ASSERT_EQUAL_UINT16(2000, m.map(in, cfg).us[0]);
    cfg.ch[0].invert = true;
    TEST_ASSERT_EQUAL_UINT16(1000, m.map(in, cfg).us[0]);
}

static void test_button_toggle() {
    MappingConfig cfg;
    cfg.ch[0].source = SourceType::Button;
    cfg.ch[0].index = 0;
    cfg.ch[0].toggle = true;
    Mapper m;
    InputState in;
    TEST_ASSERT_EQUAL_UINT16(1000, m.map(in, cfg).us[0]);
    in.buttons = 1;  // press
    TEST_ASSERT_EQUAL_UINT16(2000, m.map(in, cfg).us[0]);
    TEST_ASSERT_EQUAL_UINT16(2000, m.map(in, cfg).us[0]);  // holding does not re-toggle
    in.buttons = 0;                                        // release
    TEST_ASSERT_EQUAL_UINT16(2000, m.map(in, cfg).us[0]);
    in.buttons = 1;  // press again
    TEST_ASSERT_EQUAL_UINT16(1000, m.map(in, cfg).us[0]);
    m.reset();
    in.buttons = 0;
    TEST_ASSERT_EQUAL_UINT16(1000, m.map(in, cfg).us[0]);
}

static void test_hat_components() {
    MappingConfig cfg;
    cfg.ch[0].source = SourceType::HatX;
    cfg.ch[1].source = SourceType::HatY;
    Mapper m;
    InputState in;
    in.hat = -1;
    Frame f = m.map(in, cfg);
    TEST_ASSERT_EQUAL_UINT16(1500, f.us[0]);
    TEST_ASSERT_EQUAL_UINT16(1500, f.us[1]);
    in.hat = 1;  // up-right
    f = m.map(in, cfg);
    TEST_ASSERT_EQUAL_UINT16(2000, f.us[0]);
    TEST_ASSERT_EQUAL_UINT16(2000, f.us[1]);
    in.hat = 5;  // down-left
    f = m.map(in, cfg);
    TEST_ASSERT_EQUAL_UINT16(1000, f.us[0]);
    TEST_ASSERT_EQUAL_UINT16(1000, f.us[1]);
}

static void test_none_and_constant_sources() {
    MappingConfig cfg;
    cfg.ch[0].source = SourceType::None;
    cfg.ch[0].failsafe_us = 1234;
    cfg.ch[1].source = SourceType::Constant;
    cfg.ch[1].failsafe_us = 1900;
    Mapper m;
    InputState in;
    const Frame f = m.map(in, cfg);
    TEST_ASSERT_EQUAL_UINT16(1234, f.us[0]);
    TEST_ASSERT_EQUAL_UINT16(1900, f.us[1]);
    TEST_ASSERT_FALSE(f.failsafe);
}

static void test_failsafe_frame() {
    MappingConfig cfg;
    for (std::size_t i = 0; i < kMaxChannels; ++i) {
        cfg.ch[i].failsafe_us = static_cast<uint16_t>(1000 + i * 10);
    }
    cfg.ch[3].failsafe_us = 5000;  // out of range -> clamped
    const Frame f = Mapper::failsafeFrame(cfg);
    TEST_ASSERT_TRUE(f.failsafe);
    TEST_ASSERT_EQUAL_UINT16(1000, f.us[0]);
    TEST_ASSERT_EQUAL_UINT16(1150, f.us[15]);
    TEST_ASSERT_EQUAL_UINT16(kUsAbsoluteMax, f.us[3]);
}

static void test_default_config_maps_sidewinder_layout() {
    const Config cfg = defaultConfig();
    TEST_ASSERT_EQUAL(SourceType::Axis, cfg.mapping.ch[0].source);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(Axis::X), cfg.mapping.ch[0].index);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(Axis::Y), cfg.mapping.ch[1].index);
    TEST_ASSERT_TRUE(cfg.mapping.ch[1].invert);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(Axis::Slider), cfg.mapping.ch[2].index);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(Axis::Rz), cfg.mapping.ch[3].index);
    TEST_ASSERT_EQUAL(SourceType::Button, cfg.mapping.ch[4].source);
    TEST_ASSERT_EQUAL(SourceType::HatX, cfg.mapping.ch[6].source);
    TEST_ASSERT_EQUAL(SourceType::None, cfg.mapping.ch[15].source);
    TEST_ASSERT_EQUAL(FailsafeMode::Stop, cfg.failsafe);
    TEST_ASSERT_TRUE(cfg.sbus.enabled);
    TEST_ASSERT_TRUE(cfg.cppm.enabled);
    TEST_ASSERT_NOT_EQUAL(cfg.sbus.gpio, cfg.cppm.gpio);

    // Pulling the stick back (HID Y = +1) must give low elevator; forward gives high.
    Mapper m;
    InputState in;
    in.axis[static_cast<int>(Axis::Y)] = 1.0f;
    TEST_ASSERT_EQUAL_UINT16(1000, m.map(in, cfg.mapping).us[1]);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_axis_linear_scaling);
    RUN_TEST(test_axis_invert_endpoints_and_trim);
    RUN_TEST(test_deadband_and_expo_shaping);
    RUN_TEST(test_button_momentary);
    RUN_TEST(test_button_toggle);
    RUN_TEST(test_hat_components);
    RUN_TEST(test_none_and_constant_sources);
    RUN_TEST(test_failsafe_frame);
    RUN_TEST(test_default_config_maps_sidewinder_layout);
    return UNITY_END();
}
