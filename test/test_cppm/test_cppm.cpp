// SPDX-License-Identifier: MIT
// Unit tests for the CPPM waveform encoder and the RMT symbol packer.
#include <unity.h>

#include "rc/cppm.hpp"
#include "rc/pulse_pack.hpp"

using namespace rc;

void setUp() {}
void tearDown() {}

static uint32_t sumSegments(const cppm::Waveform& wf) {
    uint32_t total = 0;
    for (std::size_t i = 0; i < wf.count; ++i) {
        total += wf.segments[i].us;
    }
    return total;
}

static void test_standard_frame_structure() {
    cppm::Config cfg;  // 8 channels, 300 us pulses, 22.5 ms frame, idle high
    Frame frame = Frame::centered();
    const cppm::Waveform wf = cppm::encode(frame, cfg);

    TEST_ASSERT_EQUAL_UINT32(2 * (8 + 1), wf.count);
    TEST_ASSERT_EQUAL_UINT32(22500, wf.total_us);
    TEST_ASSERT_EQUAL_UINT32(22500, sumSegments(wf));
    TEST_ASSERT_TRUE(wf.idle_level);

    for (std::size_t ch = 0; ch < 8; ++ch) {
        const cppm::Segment& pulse = wf.segments[2 * ch];
        const cppm::Segment& space = wf.segments[2 * ch + 1];
        TEST_ASSERT_FALSE(pulse.level);  // pulses are low in standard PPM
        TEST_ASSERT_EQUAL_UINT32(300, pulse.us);
        TEST_ASSERT_TRUE(space.level);
        TEST_ASSERT_EQUAL_UINT32(1200, space.us);  // 1500 - 300
    }
    // Sync slot: 22500 - 8*1500 = 10500 us including its marker pulse.
    TEST_ASSERT_FALSE(wf.segments[16].level);
    TEST_ASSERT_EQUAL_UINT32(300, wf.segments[16].us);
    TEST_ASSERT_TRUE(wf.segments[17].level);
    TEST_ASSERT_EQUAL_UINT32(10200, wf.segments[17].us);
}

static void test_channel_values_are_clamped() {
    cppm::Config cfg;
    cfg.min_us = 1000;
    cfg.max_us = 2000;
    Frame frame = Frame::centered();
    frame.us[0] = 800;
    frame.us[1] = 2200;
    const cppm::Waveform wf = cppm::encode(frame, cfg);
    TEST_ASSERT_EQUAL_UINT32(1000 - 300, wf.segments[1].us);
    TEST_ASSERT_EQUAL_UINT32(2000 - 300, wf.segments[3].us);
}

static void test_frame_is_stretched_to_keep_minimum_sync() {
    cppm::Config cfg;
    cfg.channels = 16;
    cfg.frame_us = 22500;  // impossible: 16 * 2000 = 32000 us of channels
    cfg.min_sync_us = 4000;
    Frame frame;
    frame.us.fill(2000);
    const cppm::Waveform wf = cppm::encode(frame, cfg);
    TEST_ASSERT_EQUAL_UINT32(32000 + 4000, wf.total_us);
    TEST_ASSERT_EQUAL_UINT32(4000 - 300, wf.segments[wf.count - 1].us);
}

static void test_frame_is_shortened_when_sync_gap_would_be_too_long() {
    // EdgeTX only accepts a 4..19 ms sync gap: with 2 channels in a 22.5 ms frame the gap
    // would be 19.5 ms, so the encoder must shorten the frame to max_sync_us.
    cppm::Config cfg;
    cfg.channels = 2;
    cfg.frame_us = 22500;
    cfg.max_sync_us = 18000;
    const cppm::Waveform wf = cppm::encode(Frame::centered(), cfg);
    TEST_ASSERT_EQUAL_UINT32(2 * 1500 + 18000, wf.total_us);
    TEST_ASSERT_EQUAL_UINT32(18000 - 300, wf.segments[wf.count - 1].us);
}

static void test_default_sync_gap_within_edgetx_window_for_all_channel_counts() {
    for (uint8_t n = 1; n <= 16; ++n) {
        cppm::Config cfg;
        cfg.channels = n;
        Frame lo;
        lo.us.fill(1000);
        Frame hi;
        hi.us.fill(2000);
        for (const Frame* f : {&lo, &hi}) {
            const cppm::Waveform wf = cppm::encode(*f, cfg);
            const uint32_t sync = wf.segments[wf.count - 2].us + wf.segments[wf.count - 1].us;
            TEST_ASSERT_TRUE_MESSAGE(sync >= 4000 && sync <= 19000, "sync gap outside 4..19 ms");
        }
    }
}

static void test_inverted_polarity() {
    cppm::Config cfg;
    cfg.inverted = true;
    const cppm::Waveform wf = cppm::encode(Frame::centered(), cfg);
    TEST_ASSERT_FALSE(wf.idle_level);
    TEST_ASSERT_TRUE(wf.segments[0].level);
    TEST_ASSERT_FALSE(wf.segments[1].level);
}

static void test_sanitize_limits() {
    cppm::Config cfg;
    cfg.channels = 0;
    cfg.pulse_us = 5;
    cfg.frame_us = 1;
    cppm::Config s = cppm::sanitize(cfg);
    TEST_ASSERT_EQUAL_UINT8(1, s.channels);
    TEST_ASSERT_EQUAL_UINT16(100, s.pulse_us);
    TEST_ASSERT_EQUAL_UINT32(5000, s.frame_us);
    cfg.channels = 99;
    s = cppm::sanitize(cfg);
    TEST_ASSERT_EQUAL_UINT8(16, s.channels);
}

static void test_pack_one_symbol_per_slot_at_1mhz() {
    const cppm::Waveform wf = cppm::encode(Frame::centered(), cppm::Config{});
    pulse::Packed packed;
    TEST_ASSERT_TRUE(pulse::pack(wf, 1, packed));
    TEST_ASSERT_EQUAL_UINT32(9, packed.count);  // 8 channels + sync, each exactly one symbol
    for (std::size_t i = 0; i < packed.count; ++i) {
        TEST_ASSERT_FALSE(packed.symbols[i].first.level);
        TEST_ASSERT_EQUAL_UINT16(300, packed.symbols[i].first.duration);
        TEST_ASSERT_TRUE(packed.symbols[i].second.level);
        TEST_ASSERT_NOT_EQUAL(0, packed.symbols[i].second.duration);
    }
    TEST_ASSERT_EQUAL_UINT16(10200, packed.symbols[8].second.duration);
}

static void test_pack_splits_long_segments_without_zero_durations() {
    cppm::Config cfg;
    cfg.channels = 2;
    cfg.frame_us = 60000;  // sync gap of ~57 ms must be split into several halves
    const cppm::Waveform wf = cppm::encode(Frame::centered(), cfg);
    pulse::Packed packed;
    TEST_ASSERT_TRUE(pulse::pack(wf, 1, packed));
    uint32_t total = 0;
    for (std::size_t i = 0; i < packed.count; ++i) {
        TEST_ASSERT_TRUE(packed.symbols[i].first.duration >= 1);
        TEST_ASSERT_TRUE(packed.symbols[i].first.duration <= pulse::kMaxTicksPerHalf);
        TEST_ASSERT_TRUE(packed.symbols[i].second.duration >= 1);
        TEST_ASSERT_TRUE(packed.symbols[i].second.duration <= pulse::kMaxTicksPerHalf);
        total += packed.symbols[i].first.duration + packed.symbols[i].second.duration;
    }
    // The padding half (if any) adds at most 1 tick.
    TEST_ASSERT_UINT32_WITHIN(1, wf.total_us, total);
}

static void test_pack_at_higher_resolution_preserves_timing() {
    const cppm::Waveform wf = cppm::encode(Frame::centered(), cppm::Config{});
    pulse::Packed packed;
    TEST_ASSERT_TRUE(pulse::pack(wf, 10, packed));  // 10 MHz: 0.1 us ticks
    uint32_t total = 0;
    for (std::size_t i = 0; i < packed.count; ++i) {
        total += packed.symbols[i].first.duration + packed.symbols[i].second.duration;
    }
    TEST_ASSERT_UINT32_WITHIN(1, wf.total_us * 10, total);
    TEST_ASSERT_EQUAL_UINT16(3000, packed.symbols[0].first.duration);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_standard_frame_structure);
    RUN_TEST(test_channel_values_are_clamped);
    RUN_TEST(test_frame_is_stretched_to_keep_minimum_sync);
    RUN_TEST(test_frame_is_shortened_when_sync_gap_would_be_too_long);
    RUN_TEST(test_default_sync_gap_within_edgetx_window_for_all_channel_counts);
    RUN_TEST(test_inverted_polarity);
    RUN_TEST(test_sanitize_limits);
    RUN_TEST(test_pack_one_symbol_per_slot_at_1mhz);
    RUN_TEST(test_pack_splits_long_segments_without_zero_durations);
    RUN_TEST(test_pack_at_higher_resolution_preserves_timing);
    return UNITY_END();
}
