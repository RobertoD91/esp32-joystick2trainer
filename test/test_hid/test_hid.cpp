// SPDX-License-Identifier: MIT
// Unit tests for the HID report descriptor parser and the joystick decoder.
#include <unity.h>

#include <cstring>

#include "fixtures/sidewinder_ffb2_descriptor.hpp"
#include "hid/joystick_decoder.hpp"
#include "hid/report_descriptor.hpp"

using namespace hid;

void setUp() {}
void tearDown() {}

static const InputField* findField(const ParsedDescriptor& d, uint8_t report_id, uint16_t page, uint16_t usage) {
    for (std::size_t i = 0; i < d.field_count; ++i) {
        const InputField& f = d.fields[i];
        if (f.report_id == report_id && f.usage_page == page && f.usage == usage) {
            return &f;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------- SideWinder FFB2

static void test_ffb2_descriptor_parses() {
    ParsedDescriptor d;
    const ParseResult r = parse(fixtures::kSideWinderFfb2Descriptor, fixtures::kSideWinderFfb2DescriptorLen, d);
    TEST_ASSERT_EQUAL(ParseResult::Ok, r);
    TEST_ASSERT_TRUE(d.uses_report_ids);
    TEST_ASSERT_EQUAL_UINT16(kPageGenericDesktop, d.application_usage_page);
    TEST_ASSERT_EQUAL_UINT16(kUsageJoystick, d.application_usage);
    TEST_ASSERT_EQUAL_UINT32(12, d.reportBytes(1));  // 96 bits of payload
    TEST_ASSERT_EQUAL_UINT32(2, d.reportBytes(2));   // PID state report

    const InputField* x = findField(d, 1, kPageGenericDesktop, kUsageX);
    TEST_ASSERT_NOT_NULL(x);
    TEST_ASSERT_EQUAL_UINT16(0, x->bit_offset);
    TEST_ASSERT_EQUAL_UINT8(10, x->bit_size);
    TEST_ASSERT_EQUAL_INT32(-512, x->logical_min);
    TEST_ASSERT_EQUAL_INT32(511, x->logical_max);

    const InputField* y = findField(d, 1, kPageGenericDesktop, kUsageY);
    TEST_ASSERT_NOT_NULL(y);
    TEST_ASSERT_EQUAL_UINT16(16, y->bit_offset);
    TEST_ASSERT_EQUAL_UINT8(10, y->bit_size);

    const InputField* rz = findField(d, 1, kPageGenericDesktop, kUsageRz);
    TEST_ASSERT_NOT_NULL(rz);
    TEST_ASSERT_EQUAL_UINT16(32, rz->bit_offset);
    TEST_ASSERT_EQUAL_UINT8(6, rz->bit_size);
    TEST_ASSERT_EQUAL_INT32(-32, rz->logical_min);
    TEST_ASSERT_EQUAL_INT32(31, rz->logical_max);

    const InputField* slider = findField(d, 1, kPageGenericDesktop, kUsageSlider);
    TEST_ASSERT_NOT_NULL(slider);
    TEST_ASSERT_EQUAL_UINT16(40, slider->bit_offset);
    TEST_ASSERT_EQUAL_UINT8(7, slider->bit_size);
    TEST_ASSERT_EQUAL_INT32(0, slider->logical_min);
    TEST_ASSERT_EQUAL_INT32(127, slider->logical_max);

    const InputField* hat = findField(d, 1, kPageGenericDesktop, kUsageHatSwitch);
    TEST_ASSERT_NOT_NULL(hat);
    TEST_ASSERT_EQUAL_UINT16(48, hat->bit_offset);
    TEST_ASSERT_EQUAL_UINT8(4, hat->bit_size);
    TEST_ASSERT_EQUAL_INT32(7, hat->logical_max);

    for (uint16_t b = 1; b <= 8; ++b) {
        const InputField* btn = findField(d, 1, kPageButton, b);
        TEST_ASSERT_NOT_NULL(btn);
        TEST_ASSERT_EQUAL_UINT16(56 + (b - 1), btn->bit_offset);
        TEST_ASSERT_EQUAL_UINT8(1, btn->bit_size);
        TEST_ASSERT_FALSE(btn->is_array);
    }
    TEST_ASSERT_NULL(findField(d, 1, kPageButton, 9));
}

static void test_ffb2_decoder_bindings() {
    ParsedDescriptor d;
    TEST_ASSERT_EQUAL(ParseResult::Ok,
                      parse(fixtures::kSideWinderFfb2Descriptor, fixtures::kSideWinderFfb2DescriptorLen, d));
    JoystickDecoder dec;
    TEST_ASSERT_TRUE(dec.configure(d));
    TEST_ASSERT_TRUE(dec.usesReportIds());
    TEST_ASSERT_EQUAL_UINT32(8, dec.buttonCount());
    TEST_ASSERT_TRUE(dec.axisBinding(rc::Axis::X).present);
    TEST_ASSERT_TRUE(dec.axisBinding(rc::Axis::Y).present);
    TEST_ASSERT_TRUE(dec.axisBinding(rc::Axis::Rz).present);
    TEST_ASSERT_TRUE(dec.axisBinding(rc::Axis::Slider).present);
    TEST_ASSERT_FALSE(dec.axisBinding(rc::Axis::Z).present);
    TEST_ASSERT_FALSE(dec.axisBinding(rc::Axis::Throttle).present);
    TEST_ASSERT_TRUE(dec.hatBinding().present);
}

static void test_ffb2_decode_extremes() {
    ParsedDescriptor d;
    parse(fixtures::kSideWinderFfb2Descriptor, fixtures::kSideWinderFfb2DescriptorLen, d);
    JoystickDecoder dec;
    dec.configure(d);

    // Report ID 1: X = -512 (0x200), Y = +511 (0x1FF), Rz = -32 (0x20), slider = 127,
    // hat = 2 (right), buttons 1 and 3 pressed, 4 bytes of padding.
    const uint8_t report[13] = {0x01, 0x00, 0x02, 0xFF, 0x01, 0x20, 0x7F, 0x02, 0x05, 0, 0, 0, 0};
    rc::InputState s;
    TEST_ASSERT_TRUE(dec.decode(report, sizeof(report), s));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.0f, s.axis[static_cast<int>(rc::Axis::X)]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, s.axis[static_cast<int>(rc::Axis::Y)]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.0f, s.axis[static_cast<int>(rc::Axis::Rz)]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, s.axis[static_cast<int>(rc::Axis::Slider)]);
    TEST_ASSERT_TRUE(s.axis_present[static_cast<int>(rc::Axis::X)]);
    TEST_ASSERT_FALSE(s.axis_present[static_cast<int>(rc::Axis::Z)]);
    TEST_ASSERT_EQUAL_INT8(2, s.hat);
    TEST_ASSERT_EQUAL_INT8(1, s.hatX());
    TEST_ASSERT_EQUAL_INT8(0, s.hatY());
    TEST_ASSERT_EQUAL_HEX32(0x05, s.buttons);
    TEST_ASSERT_TRUE(s.button(0));
    TEST_ASSERT_FALSE(s.button(1));
    TEST_ASSERT_TRUE(s.button(2));
    TEST_ASSERT_EQUAL_UINT8(8, s.button_count);
}

static void test_ffb2_decode_center_and_hat_null() {
    ParsedDescriptor d;
    parse(fixtures::kSideWinderFfb2Descriptor, fixtures::kSideWinderFfb2DescriptorLen, d);
    JoystickDecoder dec;
    dec.configure(d);

    // X = 0, Y = 0, Rz = 0, slider = 64, hat = 8 (null state), no buttons.
    const uint8_t report[13] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x08, 0x00, 0, 0, 0, 0};
    rc::InputState s;
    s.buttons = 0xFF;  // must be cleared by the new report
    TEST_ASSERT_TRUE(dec.decode(report, sizeof(report), s));
    TEST_ASSERT_FLOAT_WITHIN(0.002f, 0.0f, s.axis[static_cast<int>(rc::Axis::X)]);
    TEST_ASSERT_FLOAT_WITHIN(0.002f, 0.0f, s.axis[static_cast<int>(rc::Axis::Y)]);
    TEST_ASSERT_FLOAT_WITHIN(0.002f, 0.0f, s.axis[static_cast<int>(rc::Axis::Rz)]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, s.axis[static_cast<int>(rc::Axis::Slider)]);
    TEST_ASSERT_EQUAL_INT8(-1, s.hat);
    TEST_ASSERT_EQUAL_HEX32(0x00, s.buttons);
}

static void test_ffb2_other_report_id_is_ignored() {
    ParsedDescriptor d;
    parse(fixtures::kSideWinderFfb2Descriptor, fixtures::kSideWinderFfb2DescriptorLen, d);
    JoystickDecoder dec;
    dec.configure(d);

    rc::InputState s;
    s.buttons = 0x81;
    s.axis[static_cast<int>(rc::Axis::X)] = 0.5f;
    const uint8_t pid_state[3] = {0x02, 0xFF, 0xFF};
    TEST_ASSERT_FALSE(dec.decode(pid_state, sizeof(pid_state), s));
    TEST_ASSERT_EQUAL_HEX32(0x81, s.buttons);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, s.axis[static_cast<int>(rc::Axis::X)]);
}

static void test_ffb2_truncated_report_is_safe() {
    ParsedDescriptor d;
    parse(fixtures::kSideWinderFfb2Descriptor, fixtures::kSideWinderFfb2DescriptorLen, d);
    JoystickDecoder dec;
    dec.configure(d);

    // Only X and Y fit; the rest must be skipped without reading out of bounds.
    const uint8_t report[5] = {0x01, 0xFF, 0x01, 0x00, 0x02};
    rc::InputState s;
    s.buttons = 0x01;
    TEST_ASSERT_TRUE(dec.decode(report, sizeof(report), s));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, s.axis[static_cast<int>(rc::Axis::X)]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.0f, s.axis[static_cast<int>(rc::Axis::Y)]);
    TEST_ASSERT_EQUAL_HEX32(0x01, s.buttons);  // untouched
}

// ---------------------------------------------------------------- generic gamepad, no report IDs

static const uint8_t kGamepadDescriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x05,        // Usage (Gamepad)
    0xA1, 0x01,        // Collection (Application)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x0C,        //   Report Count (12)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (1)
    0x29, 0x0C,        //   Usage Maximum (12)
    0x81, 0x02,        //   Input (Data,Var,Abs)          -> bits 0..11
    0x95, 0x04,        //   Report Count (4)
    0x81, 0x01,        //   Input (Const)                 -> bits 12..15
    0x05, 0x01,        //   Usage Page (Generic Desktop)
    0x25, 0x07,        //   Logical Maximum (7)
    0x46, 0x3B, 0x01,  //   Physical Maximum (315)
    0x75, 0x04,        //   Report Size (4)
    0x95, 0x01,        //   Report Count (1)
    0x65, 0x14,        //   Unit (degrees)
    0x09, 0x39,        //   Usage (Hat switch)
    0x81, 0x42,        //   Input (Data,Var,Abs,Null)     -> bits 16..19
    0x65, 0x00,        //   Unit (none)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x04,        //   Report Size (4)
    0x81, 0x01,        //   Input (Const)                 -> bits 20..23
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x46, 0xFF, 0x00,  //   Physical Maximum (255)
    0x09, 0x30,        //   Usage (X)
    0x09, 0x31,        //   Usage (Y)
    0x09, 0x32,        //   Usage (Z)
    0x09, 0x35,        //   Usage (Rz)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x04,        //   Report Count (4)
    0x81, 0x02,        //   Input (Data,Var,Abs)          -> bytes 3..6
    0xC0,              // End Collection
};

static void test_gamepad_without_report_ids() {
    ParsedDescriptor d;
    TEST_ASSERT_EQUAL(ParseResult::Ok, parse(kGamepadDescriptor, sizeof(kGamepadDescriptor), d));
    TEST_ASSERT_FALSE(d.uses_report_ids);
    TEST_ASSERT_EQUAL_UINT16(kUsageGamepad, d.application_usage);
    TEST_ASSERT_EQUAL_UINT32(7, d.reportBytes(0));

    JoystickDecoder dec;
    TEST_ASSERT_TRUE(dec.configure(d));
    TEST_ASSERT_EQUAL_UINT32(12, dec.buttonCount());
    TEST_ASSERT_EQUAL_UINT16(24, dec.axisBinding(rc::Axis::X).field.bit_offset);
    TEST_ASSERT_EQUAL_UINT16(48, dec.axisBinding(rc::Axis::Rz).field.bit_offset);

    // buttons 1, 3 and 12; hat = 6 (left); X=0, Y=255, Z=128, Rz=64
    const uint8_t report[7] = {0x05, 0x08, 0x06, 0x00, 0xFF, 0x80, 0x40};
    rc::InputState s;
    TEST_ASSERT_TRUE(dec.decode(report, sizeof(report), s));
    TEST_ASSERT_EQUAL_HEX32((1u << 0) | (1u << 2) | (1u << 11), s.buttons);
    TEST_ASSERT_EQUAL_INT8(6, s.hat);
    TEST_ASSERT_EQUAL_INT8(-1, s.hatX());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.0f, s.axis[static_cast<int>(rc::Axis::X)]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, s.axis[static_cast<int>(rc::Axis::Y)]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, s.axis[static_cast<int>(rc::Axis::Z)]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -0.5f, s.axis[static_cast<int>(rc::Axis::Rz)]);
}

// ---------------------------------------------------------------- array-style buttons

static const uint8_t kArrayButtonDescriptor[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x04,  // Usage (Joystick)
    0xA1, 0x01,  // Collection (Application)
    0x05, 0x09,  //   Usage Page (Button)
    0x19, 0x01,  //   Usage Minimum (1)
    0x29, 0x10,  //   Usage Maximum (16)
    0x15, 0x01,  //   Logical Minimum (1)
    0x25, 0x10,  //   Logical Maximum (16)
    0x75, 0x08,  //   Report Size (8)
    0x95, 0x02,  //   Report Count (2)
    0x81, 0x00,  //   Input (Data,Array,Abs)  -> two bytes, each the number of a pressed button
    0x05, 0x01,  //   Usage Page (Generic Desktop)
    0x09, 0x30,  //   Usage (X)
    0x15, 0x81,  //   Logical Minimum (-127)
    0x25, 0x7F,  //   Logical Maximum (127)
    0x75, 0x08,  //   Report Size (8)
    0x95, 0x01,  //   Report Count (1)
    0x81, 0x02,  //   Input (Data,Var,Abs)
    0xC0,        // End Collection
};

static void test_array_buttons_and_signed_axis() {
    ParsedDescriptor d;
    TEST_ASSERT_EQUAL(ParseResult::Ok, parse(kArrayButtonDescriptor, sizeof(kArrayButtonDescriptor), d));
    TEST_ASSERT_EQUAL_UINT32(2, d.field_count);
    TEST_ASSERT_TRUE(d.fields[0].is_array);
    TEST_ASSERT_EQUAL_UINT8(2, d.fields[0].count);
    TEST_ASSERT_EQUAL_UINT16(1, d.fields[0].usage);
    TEST_ASSERT_EQUAL_UINT16(16, d.fields[0].usage_max);

    JoystickDecoder dec;
    TEST_ASSERT_TRUE(dec.configure(d));
    TEST_ASSERT_EQUAL_UINT32(16, dec.buttonCount());

    rc::InputState s;
    const uint8_t report1[3] = {3, 7, 0x7F};
    TEST_ASSERT_TRUE(dec.decode(report1, sizeof(report1), s));
    TEST_ASSERT_EQUAL_HEX32((1u << 2) | (1u << 6), s.buttons);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, s.axis[static_cast<int>(rc::Axis::X)]);

    const uint8_t report2[3] = {0, 0, 0x81};  // released, X = -127
    TEST_ASSERT_TRUE(dec.decode(report2, sizeof(report2), s));
    TEST_ASSERT_EQUAL_HEX32(0, s.buttons);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.0f, s.axis[static_cast<int>(rc::Axis::X)]);
}

// ---------------------------------------------------------------- robustness

static void test_parser_error_paths() {
    ParsedDescriptor d;
    TEST_ASSERT_EQUAL(ParseResult::NoInputFields, parse(nullptr, 0, d));
    const uint8_t empty[1] = {0xC0};
    TEST_ASSERT_EQUAL(ParseResult::NoInputFields, parse(empty, sizeof(empty), d));
    // Item announcing 2 data bytes but the descriptor ends early.
    const uint8_t truncated[2] = {0x05, 0x01};
    TEST_ASSERT_EQUAL(ParseResult::Truncated, parse(truncated, 1, d));
    // Cutting the FFB2 descriptor in the middle of an item must not crash.
    TEST_ASSERT_EQUAL(ParseResult::Truncated, parse(fixtures::kSideWinderFfb2Descriptor, 33, d));
    const uint8_t pop_without_push[] = {0x05, 0x01, 0xB4, 0x00};
    TEST_ASSERT_EQUAL(ParseResult::StackOverflow, parse(pop_without_push, sizeof(pop_without_push), d));
}

static void test_decoder_rejects_empty_input() {
    ParsedDescriptor d;
    parse(kGamepadDescriptor, sizeof(kGamepadDescriptor), d);
    JoystickDecoder dec;
    dec.configure(d);
    rc::InputState s;
    TEST_ASSERT_FALSE(dec.decode(nullptr, 0, s));
    const uint8_t one = 0;
    TEST_ASSERT_FALSE(dec.decode(&one, 0, s));
}

static void test_field_extraction_sign_extension() {
    InputField f;
    f.bit_offset = 4;
    f.bit_size = 6;
    f.logical_min = -32;
    f.logical_max = 31;
    // bits 4..9 of the payload = 0b100000 (-32)
    const uint8_t payload[2] = {0x00, 0x02};
    bool ok = false;
    TEST_ASSERT_EQUAL_INT32(-32, JoystickDecoder::extract(payload, sizeof(payload), f, 0, ok));
    TEST_ASSERT_TRUE(ok);
    f.logical_min = 0;
    TEST_ASSERT_EQUAL_INT32(32, JoystickDecoder::extract(payload, sizeof(payload), f, 0, ok));
    // Out of bounds element
    JoystickDecoder::extract(payload, sizeof(payload), f, 2, ok);
    TEST_ASSERT_FALSE(ok);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_ffb2_descriptor_parses);
    RUN_TEST(test_ffb2_decoder_bindings);
    RUN_TEST(test_ffb2_decode_extremes);
    RUN_TEST(test_ffb2_decode_center_and_hat_null);
    RUN_TEST(test_ffb2_other_report_id_is_ignored);
    RUN_TEST(test_ffb2_truncated_report_is_safe);
    RUN_TEST(test_gamepad_without_report_ids);
    RUN_TEST(test_array_buttons_and_signed_axis);
    RUN_TEST(test_parser_error_paths);
    RUN_TEST(test_decoder_rejects_empty_input);
    RUN_TEST(test_field_extraction_sign_extension);
    return UNITY_END();
}
