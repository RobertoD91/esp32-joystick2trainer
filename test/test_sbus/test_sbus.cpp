// SPDX-License-Identifier: MIT
// Unit tests for the SBUS encoder/decoder.
#include <unity.h>

#include <cstring>

#include "rc/sbus.hpp"

using namespace rc;

void setUp() {}
void tearDown() {}

static void test_us_conversion_reference_points() {
    TEST_ASSERT_EQUAL_UINT16(992, sbus::fromUs(1500));
    TEST_ASSERT_EQUAL_UINT16(173, sbus::fromUs(988));  // 172.8 rounds to 173
    TEST_ASSERT_EQUAL_UINT16(1811, sbus::fromUs(2012));
    TEST_ASSERT_EQUAL_UINT16(192, sbus::fromUs(1000));
    TEST_ASSERT_EQUAL_UINT16(1792, sbus::fromUs(2000));
    TEST_ASSERT_EQUAL_UINT16(0, sbus::fromUs(800));      // clamped
    TEST_ASSERT_EQUAL_UINT16(2047, sbus::fromUs(2200));  // clamped
    TEST_ASSERT_EQUAL_UINT16(1500, sbus::toUs(992));
    TEST_ASSERT_EQUAL_UINT16(988, sbus::toUs(172));
    TEST_ASSERT_EQUAL_UINT16(2012, sbus::toUs(1811));
}

static void test_us_conversion_roundtrip_is_monotonic() {
    uint16_t last = 0;
    for (uint16_t us = 880; us <= 2159; ++us) {
        const uint16_t v = sbus::fromUs(us);
        TEST_ASSERT_TRUE(v >= last);
        last = v;
        const uint16_t back = sbus::toUs(v);
        TEST_ASSERT_INT_WITHIN(1, us, back);
    }
}

static void test_frame_layout_all_center() {
    sbus::Values values;
    values.fill(992);
    sbus::Buffer buf;
    sbus::encode(values, sbus::Flags{}, buf);

    TEST_ASSERT_EQUAL_HEX8(0x0F, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[23]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[24]);
    // 992 = 0b011_1110_0000. Packing 16 x 11 bits LSB-first gives the well known
    // "E0 03 1F F8 C0 07 3E F0 81 0F 7C E0 03 1F F8 C0 07 3E F0 81 0F 7C" pattern.
    const uint8_t expected[22] = {0xE0, 0x03, 0x1F, 0xF8, 0xC0, 0x07, 0x3E, 0xF0, 0x81, 0x0F, 0x7C,
                                  0xE0, 0x03, 0x1F, 0xF8, 0xC0, 0x07, 0x3E, 0xF0, 0x81, 0x0F, 0x7C};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, &buf[1], 22);
}

static void test_frame_first_channel_bits() {
    sbus::Values values{};
    values[0] = 0x7FF;  // all 11 bits set
    sbus::Buffer buf;
    sbus::encode(values, sbus::Flags{}, buf);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x07, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[3]);

    values.fill(0);
    values[1] = 1;  // channel 2 starts at bit 11 -> bit 3 of byte 2
    sbus::encode(values, sbus::Flags{}, buf);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x08, buf[2]);

    values.fill(0);
    values[15] = 0x7FF;  // channel 16 occupies the top 11 bits of bytes 21..22
    sbus::encode(values, sbus::Flags{}, buf);
    TEST_ASSERT_EQUAL_HEX8(0xE0, buf[21]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[22]);
}

static void test_flags() {
    sbus::Values values{};
    sbus::Buffer buf;
    sbus::Flags flags;
    flags.ch17 = true;
    flags.failsafe = true;
    sbus::encode(values, flags, buf);
    TEST_ASSERT_EQUAL_HEX8(0x09, buf[23]);
    flags = sbus::Flags{};
    flags.ch18 = true;
    flags.frame_lost = true;
    sbus::encode(values, flags, buf);
    TEST_ASSERT_EQUAL_HEX8(0x06, buf[23]);
}

static void test_encode_decode_roundtrip() {
    sbus::Values values;
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = static_cast<uint16_t>(100 + i * 113);
    }
    sbus::Flags flags;
    flags.frame_lost = true;
    sbus::Buffer buf;
    sbus::encode(values, flags, buf);

    sbus::Values decoded;
    sbus::Flags decoded_flags;
    TEST_ASSERT_TRUE(sbus::decode(buf.data(), buf.size(), decoded, decoded_flags));
    TEST_ASSERT_EQUAL_UINT16_ARRAY(values.data(), decoded.data(), values.size());
    TEST_ASSERT_TRUE(decoded_flags.frame_lost);
    TEST_ASSERT_FALSE(decoded_flags.failsafe);
}

static void test_decode_rejects_bad_frames() {
    sbus::Buffer buf{};
    sbus::Values values;
    sbus::Flags flags;
    TEST_ASSERT_FALSE(sbus::decode(buf.data(), buf.size(), values, flags));  // header missing
    buf[0] = 0x0F;
    buf[24] = 0x04;
    TEST_ASSERT_FALSE(sbus::decode(buf.data(), buf.size(), values, flags));  // footer wrong
    buf[24] = 0x00;
    TEST_ASSERT_FALSE(sbus::decode(buf.data(), 24, values, flags));  // too short
    TEST_ASSERT_TRUE(sbus::decode(buf.data(), buf.size(), values, flags));
}

static void test_encode_frame_from_microseconds() {
    Frame frame = Frame::centered();
    frame.us[0] = 1000;
    frame.us[1] = 2000;
    sbus::Buffer buf;
    sbus::encodeFrame(frame, sbus::Flags{}, buf);
    sbus::Values decoded;
    sbus::Flags flags;
    TEST_ASSERT_TRUE(sbus::decode(buf.data(), buf.size(), decoded, flags));
    TEST_ASSERT_EQUAL_UINT16(192, decoded[0]);
    TEST_ASSERT_EQUAL_UINT16(1792, decoded[1]);
    TEST_ASSERT_EQUAL_UINT16(992, decoded[2]);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_us_conversion_reference_points);
    RUN_TEST(test_us_conversion_roundtrip_is_monotonic);
    RUN_TEST(test_frame_layout_all_center);
    RUN_TEST(test_frame_first_channel_bits);
    RUN_TEST(test_flags);
    RUN_TEST(test_encode_decode_roundtrip);
    RUN_TEST(test_decode_rejects_bad_frames);
    RUN_TEST(test_encode_frame_from_microseconds);
    return UNITY_END();
}
