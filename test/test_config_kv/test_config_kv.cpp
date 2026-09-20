// SPDX-License-Identifier: MIT
// Unit tests for the key/value configuration interface used by the serial console.
#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "rc/config.hpp"
#include "rc/config_kv.hpp"

using namespace rc;

void setUp() {}
void tearDown() {}

static std::vector<std::pair<std::string, std::string>> dump(const Config& cfg, const char* prefix = nullptr) {
    std::vector<std::pair<std::string, std::string>> out;
    kv::forEach(
        cfg, prefix,
        [](const char* key, const char* value, void* ctx) {
            static_cast<std::vector<std::pair<std::string, std::string>>*>(ctx)->emplace_back(key, value);
        },
        &out);
    return out;
}

static void test_dump_roundtrip_reproduces_defaults() {
    const Config original = defaultConfig();
    const auto entries = dump(original);
    TEST_ASSERT_EQUAL_UINT32(19 + 16 * 9, entries.size());

    Config rebuilt;  // all zero except version -> everything differs from the defaults
    rebuilt.mapping = MappingConfig{};
    rebuilt.sbus.enabled = false;
    rebuilt.cppm.enabled = false;
    char err[64];
    for (const auto& [key, value] : entries) {
        TEST_ASSERT_EQUAL_MESSAGE(kv::Result::Ok, kv::set(rebuilt, key.c_str(), value.c_str(), err, sizeof(err)),
                                  key.c_str());
    }
    const auto entries2 = dump(rebuilt);
    TEST_ASSERT_EQUAL_UINT32(entries.size(), entries2.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        TEST_ASSERT_EQUAL_STRING(entries[i].first.c_str(), entries2[i].first.c_str());
        TEST_ASSERT_EQUAL_STRING_MESSAGE(entries[i].second.c_str(), entries2[i].second.c_str(),
                                         entries[i].first.c_str());
    }
}

static void test_global_keys() {
    Config cfg = defaultConfig();
    char err[64];
    char out[32];

    TEST_ASSERT_EQUAL(kv::Result::Ok, kv::set(cfg, "sbus.gpio", "21", err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT8(21, cfg.sbus.gpio);
    TEST_ASSERT_EQUAL(kv::Result::Ok, kv::set(cfg, "sbus.period", "7000", err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT32(7000, cfg.sbus.period_us);
    TEST_ASSERT_EQUAL(kv::Result::Ok, kv::set(cfg, "sbus.inverted", "off", err, sizeof(err)));
    TEST_ASSERT_FALSE(cfg.sbus.inverted);
    TEST_ASSERT_EQUAL(kv::Result::Ok, kv::set(cfg, "cppm.channels", "12", err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT8(12, cfg.cppm.wave.channels);
    TEST_ASSERT_EQUAL(kv::Result::Ok, kv::set(cfg, "cppm.inverted", "1", err, sizeof(err)));
    TEST_ASSERT_TRUE(cfg.cppm.wave.inverted);
    TEST_ASSERT_EQUAL(kv::Result::Ok, kv::set(cfg, "failsafe.mode", "preset", err, sizeof(err)));
    TEST_ASSERT_EQUAL(FailsafeMode::Preset, cfg.failsafe);
    TEST_ASSERT_TRUE(kv::get(cfg, "failsafe.mode", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("preset", out);
    TEST_ASSERT_EQUAL(kv::Result::Ok, kv::set(cfg, "input.timeout", "500", err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT32(500, cfg.input_timeout_ms);
    TEST_ASSERT_EQUAL(kv::Result::Ok, kv::set(cfg, "led.gpio", "-1", err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT8(-1, cfg.led_gpio);
}

static void test_channel_keys() {
    Config cfg = defaultConfig();
    char err[64];
    char out[32];

    TEST_ASSERT_EQUAL(kv::Result::Ok, kv::set(cfg, "ch5.src", "axis:throttle", err, sizeof(err)));
    TEST_ASSERT_EQUAL(SourceType::Axis, cfg.mapping.ch[4].source);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(Axis::Throttle), cfg.mapping.ch[4].index);
    TEST_ASSERT_TRUE(kv::get(cfg, "ch5.src", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("axis:throttle", out);

    TEST_ASSERT_EQUAL(kv::Result::Ok, kv::set(cfg, "ch16.src", "btn:32", err, sizeof(err)));
    TEST_ASSERT_EQUAL(SourceType::Button, cfg.mapping.ch[15].source);
    TEST_ASSERT_EQUAL_UINT8(31, cfg.mapping.ch[15].index);
    TEST_ASSERT_TRUE(kv::get(cfg, "ch16.src", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("btn:32", out);

    TEST_ASSERT_EQUAL(kv::Result::Ok, kv::set(cfg, "ch1.src", "haty", err, sizeof(err)));
    TEST_ASSERT_EQUAL(SourceType::HatY, cfg.mapping.ch[0].source);
    TEST_ASSERT_EQUAL(kv::Result::Ok, kv::set(cfg, "ch1.invert", "1", err, sizeof(err)));
    TEST_ASSERT_TRUE(cfg.mapping.ch[0].invert);
    TEST_ASSERT_EQUAL(kv::Result::Ok, kv::set(cfg, "ch1.deadband", "10", err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT8(10, cfg.mapping.ch[0].deadband_pct);
    TEST_ASSERT_EQUAL(kv::Result::Ok, kv::set(cfg, "ch1.expo", "30", err, sizeof(err)));
    TEST_ASSERT_EQUAL(kv::Result::Ok, kv::set(cfg, "ch1.min", "1100", err, sizeof(err)));
    TEST_ASSERT_EQUAL(kv::Result::Ok, kv::set(cfg, "ch1.max", "1900", err, sizeof(err)));
    TEST_ASSERT_EQUAL(kv::Result::Ok, kv::set(cfg, "ch1.trim", "-25", err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT16(-25, cfg.mapping.ch[0].trim_us);
    TEST_ASSERT_EQUAL(kv::Result::Ok, kv::set(cfg, "ch1.failsafe", "1000", err, sizeof(err)));
    TEST_ASSERT_EQUAL(kv::Result::Ok, kv::set(cfg, "ch1.toggle", "yes", err, sizeof(err)));
    TEST_ASSERT_TRUE(cfg.mapping.ch[0].toggle);
}

static void test_invalid_keys_and_values() {
    Config cfg = defaultConfig();
    const Config before = cfg;
    char err[64] = {0};

    TEST_ASSERT_EQUAL(kv::Result::UnknownKey, kv::set(cfg, "bogus.key", "1", err, sizeof(err)));
    TEST_ASSERT_EQUAL(kv::Result::UnknownKey, kv::set(cfg, "ch0.src", "none", err, sizeof(err)));
    TEST_ASSERT_EQUAL(kv::Result::UnknownKey, kv::set(cfg, "ch17.src", "none", err, sizeof(err)));
    TEST_ASSERT_EQUAL(kv::Result::UnknownKey, kv::set(cfg, "ch3.colour", "red", err, sizeof(err)));
    TEST_ASSERT_EQUAL(kv::Result::BadValue, kv::set(cfg, "ch3.src", "axis:nope", err, sizeof(err)));
    TEST_ASSERT_EQUAL(kv::Result::BadValue, kv::set(cfg, "ch3.src", "btn:0", err, sizeof(err)));
    TEST_ASSERT_EQUAL(kv::Result::BadValue, kv::set(cfg, "ch3.src", "btn:33", err, sizeof(err)));
    TEST_ASSERT_EQUAL(kv::Result::BadValue, kv::set(cfg, "ch3.deadband", "51", err, sizeof(err)));
    TEST_ASSERT_EQUAL(kv::Result::BadValue, kv::set(cfg, "ch3.min", "abc", err, sizeof(err)));
    TEST_ASSERT_EQUAL(kv::Result::BadValue, kv::set(cfg, "sbus.gpio", "99", err, sizeof(err)));
    TEST_ASSERT_EQUAL(kv::Result::BadValue, kv::set(cfg, "sbus.enabled", "maybe", err, sizeof(err)));
    TEST_ASSERT_EQUAL(kv::Result::BadValue, kv::set(cfg, "failsafe.mode", "panic", err, sizeof(err)));
    TEST_ASSERT_EQUAL(kv::Result::BadValue, kv::set(cfg, "cppm.frame", "100", err, sizeof(err)));
    TEST_ASSERT_TRUE(std::strlen(err) > 0);

    // Nothing changed.
    TEST_ASSERT_EQUAL_MEMORY(&before.mapping, &cfg.mapping, sizeof(MappingConfig));
    TEST_ASSERT_EQUAL_INT8(before.sbus.gpio, cfg.sbus.gpio);
    TEST_ASSERT_EQUAL(before.failsafe, cfg.failsafe);

    char out[8];
    TEST_ASSERT_FALSE(kv::get(cfg, "nothing", out, sizeof(out)));
}

static void test_prefix_filter() {
    const Config cfg = defaultConfig();
    const auto ch3 = dump(cfg, "ch3.");
    TEST_ASSERT_EQUAL_UINT32(9, ch3.size());
    TEST_ASSERT_EQUAL_STRING("ch3.src", ch3[0].first.c_str());
    const auto sbus = dump(cfg, "sbus");
    TEST_ASSERT_EQUAL_UINT32(5, sbus.size());
    const auto ch1x = dump(cfg, "ch1");            // must not include ch10..ch16 keys? It does by prefix: check count
    TEST_ASSERT_EQUAL_UINT32(9 * 8, ch1x.size());  // ch1, ch10..ch16
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_dump_roundtrip_reproduces_defaults);
    RUN_TEST(test_global_keys);
    RUN_TEST(test_channel_keys);
    RUN_TEST(test_invalid_keys_and_values);
    RUN_TEST(test_prefix_filter);
    return UNITY_END();
}
