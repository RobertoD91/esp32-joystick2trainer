// SPDX-License-Identifier: MIT
#include "rc/config_kv.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace rc::kv {

namespace {

void setErr(char* err, std::size_t len, const char* msg) {
    if (err != nullptr && len > 0) {
        std::snprintf(err, len, "%s", msg);
    }
}

bool parseInt(const char* text, long min, long max, long& out) {
    if (text == nullptr || *text == '\0') {
        return false;
    }
    char* end = nullptr;
    const long v = std::strtol(text, &end, 0);
    if (end == text || *end != '\0' || v < min || v > max) {
        return false;
    }
    out = v;
    return true;
}

bool parseBool(const char* text, bool& out) {
    if (text == nullptr) {
        return false;
    }
    if (std::strcmp(text, "1") == 0 || std::strcmp(text, "on") == 0 || std::strcmp(text, "true") == 0 ||
        std::strcmp(text, "yes") == 0) {
        out = true;
        return true;
    }
    if (std::strcmp(text, "0") == 0 || std::strcmp(text, "off") == 0 || std::strcmp(text, "false") == 0 ||
        std::strcmp(text, "no") == 0) {
        out = false;
        return true;
    }
    return false;
}

// Parses "chN.field" -> N (1..16) and field. Returns false when the key is not a channel key.
bool splitChannelKey(const char* key, std::size_t& index, const char*& field) {
    if (std::strncmp(key, "ch", 2) != 0) {
        return false;
    }
    const char* p = key + 2;
    char* end = nullptr;
    const long n = std::strtol(p, &end, 10);
    if (end == p || *end != '.' || n < 1 || n > static_cast<long>(kMaxChannels)) {
        return false;
    }
    index = static_cast<std::size_t>(n - 1);
    field = end + 1;
    return true;
}

template <typename T>
bool assignInt(const char* value, long min, long max, T& target, char* err, std::size_t err_len) {
    long v = 0;
    if (!parseInt(value, min, max, v)) {
        char msg[64];
        std::snprintf(msg, sizeof(msg), "expected integer %ld..%ld", min, max);
        setErr(err, err_len, msg);
        return false;
    }
    target = static_cast<T>(v);
    return true;
}

bool assignBool(const char* value, bool& target, char* err, std::size_t err_len) {
    bool b = false;
    if (!parseBool(value, b)) {
        setErr(err, err_len, "expected 0/1");
        return false;
    }
    target = b;
    return true;
}

Result setChannel(ChannelConfig& ch, const char* field, const char* value, char* err, std::size_t err_len) {
    if (std::strcmp(field, "src") == 0) {
        ChannelConfig tmp = ch;
        if (!parseSource(value, tmp)) {
            setErr(err, err_len, "expected none|const|axis:<name>|btn:<n>|hatx|haty");
            return Result::BadValue;
        }
        ch = tmp;
        return Result::Ok;
    }
    if (std::strcmp(field, "invert") == 0) {
        return assignBool(value, ch.invert, err, err_len) ? Result::Ok : Result::BadValue;
    }
    if (std::strcmp(field, "toggle") == 0) {
        return assignBool(value, ch.toggle, err, err_len) ? Result::Ok : Result::BadValue;
    }
    if (std::strcmp(field, "deadband") == 0) {
        return assignInt(value, 0, 50, ch.deadband_pct, err, err_len) ? Result::Ok : Result::BadValue;
    }
    if (std::strcmp(field, "expo") == 0) {
        return assignInt(value, 0, 100, ch.expo_pct, err, err_len) ? Result::Ok : Result::BadValue;
    }
    if (std::strcmp(field, "min") == 0) {
        return assignInt(value, kUsAbsoluteMin, kUsAbsoluteMax, ch.min_us, err, err_len) ? Result::Ok
                                                                                         : Result::BadValue;
    }
    if (std::strcmp(field, "max") == 0) {
        return assignInt(value, kUsAbsoluteMin, kUsAbsoluteMax, ch.max_us, err, err_len) ? Result::Ok
                                                                                         : Result::BadValue;
    }
    if (std::strcmp(field, "trim") == 0) {
        return assignInt(value, -500, 500, ch.trim_us, err, err_len) ? Result::Ok : Result::BadValue;
    }
    if (std::strcmp(field, "failsafe") == 0) {
        return assignInt(value, kUsAbsoluteMin, kUsAbsoluteMax, ch.failsafe_us, err, err_len) ? Result::Ok
                                                                                              : Result::BadValue;
    }
    setErr(err, err_len, "unknown channel field");
    return Result::UnknownKey;
}

bool getChannel(const ChannelConfig& ch, const char* field, char* out, std::size_t out_len) {
    if (std::strcmp(field, "src") == 0) {
        formatSource(ch, out, out_len);
        return true;
    }
    long v = 0;
    if (std::strcmp(field, "invert") == 0) {
        v = ch.invert ? 1 : 0;
    } else if (std::strcmp(field, "toggle") == 0) {
        v = ch.toggle ? 1 : 0;
    } else if (std::strcmp(field, "deadband") == 0) {
        v = ch.deadband_pct;
    } else if (std::strcmp(field, "expo") == 0) {
        v = ch.expo_pct;
    } else if (std::strcmp(field, "min") == 0) {
        v = ch.min_us;
    } else if (std::strcmp(field, "max") == 0) {
        v = ch.max_us;
    } else if (std::strcmp(field, "trim") == 0) {
        v = ch.trim_us;
    } else if (std::strcmp(field, "failsafe") == 0) {
        v = ch.failsafe_us;
    } else {
        return false;
    }
    std::snprintf(out, out_len, "%ld", v);
    return true;
}

constexpr const char* kGlobalKeys[] = {
    "sbus.enabled", "sbus.gpio",     "sbus.period",   "sbus.inverted", "sbus.flags",    "cppm.enabled",
    "cppm.gpio",    "cppm.channels", "cppm.frame",    "cppm.pulse",    "cppm.inverted", "cppm.min",
    "cppm.max",     "cppm.sync_min", "cppm.sync_max", "failsafe.mode", "input.timeout", "led.gpio",
    "log.input",    "ffb.enabled",   "ffb.spring",    "ffb.gain",      "ffb.deadband",
};

constexpr std::size_t kGlobalKeyCount = sizeof(kGlobalKeys) / sizeof(kGlobalKeys[0]);

constexpr const char* kChannelFields[] = {"src", "invert", "toggle", "deadband", "expo",
                                          "min", "max",    "trim",   "failsafe"};

}  // namespace

void formatSource(const ChannelConfig& ch, char* out, std::size_t out_len) {
    switch (ch.source) {
        case SourceType::Axis:
            std::snprintf(out, out_len, "axis:%s", axisName(static_cast<Axis>(ch.index)));
            break;
        case SourceType::Button:
            std::snprintf(out, out_len, "btn:%u", static_cast<unsigned>(ch.index) + 1);
            break;
        case SourceType::HatX:
            std::snprintf(out, out_len, "hatx");
            break;
        case SourceType::HatY:
            std::snprintf(out, out_len, "haty");
            break;
        case SourceType::Constant:
            std::snprintf(out, out_len, "const");
            break;
        case SourceType::None:
        default:
            std::snprintf(out, out_len, "none");
            break;
    }
}

bool parseSource(const char* text, ChannelConfig& ch) {
    if (text == nullptr) {
        return false;
    }
    if (std::strcmp(text, "none") == 0) {
        ch.source = SourceType::None;
        ch.index = 0;
        return true;
    }
    if (std::strcmp(text, "const") == 0) {
        ch.source = SourceType::Constant;
        ch.index = 0;
        return true;
    }
    if (std::strcmp(text, "hatx") == 0) {
        ch.source = SourceType::HatX;
        ch.index = 0;
        return true;
    }
    if (std::strcmp(text, "haty") == 0) {
        ch.source = SourceType::HatY;
        ch.index = 0;
        return true;
    }
    if (std::strncmp(text, "axis:", 5) == 0) {
        Axis axis;
        if (!axisFromName(text + 5, axis)) {
            return false;
        }
        ch.source = SourceType::Axis;
        ch.index = static_cast<uint8_t>(axis);
        return true;
    }
    if (std::strncmp(text, "btn:", 4) == 0) {
        long n = 0;
        if (!parseInt(text + 4, 1, static_cast<long>(kMaxButtons), n)) {
            return false;
        }
        ch.source = SourceType::Button;
        ch.index = static_cast<uint8_t>(n - 1);
        return true;
    }
    return false;
}

Result set(Config& cfg, const char* key, const char* value, char* err, std::size_t err_len) {
    if (key == nullptr || value == nullptr) {
        setErr(err, err_len, "missing key/value");
        return Result::BadValue;
    }
    std::size_t idx = 0;
    const char* field = nullptr;
    if (splitChannelKey(key, idx, field)) {
        return setChannel(cfg.mapping.ch[idx], field, value, err, err_len);
    }

    auto ok = [](bool b) { return b ? Result::Ok : Result::BadValue; };
    if (std::strcmp(key, "sbus.enabled") == 0) {
        return ok(assignBool(value, cfg.sbus.enabled, err, err_len));
    }
    if (std::strcmp(key, "sbus.gpio") == 0) {
        return ok(assignInt(value, -1, 48, cfg.sbus.gpio, err, err_len));
    }
    if (std::strcmp(key, "sbus.period") == 0) {
        return ok(assignInt(value, 3000, 50000, cfg.sbus.period_us, err, err_len));
    }
    if (std::strcmp(key, "sbus.inverted") == 0) {
        return ok(assignBool(value, cfg.sbus.inverted, err, err_len));
    }
    if (std::strcmp(key, "sbus.flags") == 0) {
        return ok(assignBool(value, cfg.sbus.failsafe_flags, err, err_len));
    }
    if (std::strcmp(key, "cppm.enabled") == 0) {
        return ok(assignBool(value, cfg.cppm.enabled, err, err_len));
    }
    if (std::strcmp(key, "cppm.gpio") == 0) {
        return ok(assignInt(value, -1, 48, cfg.cppm.gpio, err, err_len));
    }
    if (std::strcmp(key, "cppm.channels") == 0) {
        return ok(assignInt(value, cppm::kMinChannels, cppm::kMaxChannels, cfg.cppm.wave.channels, err, err_len));
    }
    if (std::strcmp(key, "cppm.frame") == 0) {
        return ok(assignInt(value, 5000, 60000, cfg.cppm.wave.frame_us, err, err_len));
    }
    if (std::strcmp(key, "cppm.pulse") == 0) {
        return ok(assignInt(value, 100, 600, cfg.cppm.wave.pulse_us, err, err_len));
    }
    if (std::strcmp(key, "cppm.inverted") == 0) {
        return ok(assignBool(value, cfg.cppm.wave.inverted, err, err_len));
    }
    if (std::strcmp(key, "cppm.min") == 0) {
        return ok(assignInt(value, kUsAbsoluteMin, kUsAbsoluteMax, cfg.cppm.wave.min_us, err, err_len));
    }
    if (std::strcmp(key, "cppm.max") == 0) {
        return ok(assignInt(value, kUsAbsoluteMin, kUsAbsoluteMax, cfg.cppm.wave.max_us, err, err_len));
    }
    if (std::strcmp(key, "cppm.sync_min") == 0) {
        return ok(assignInt(value, 2000, 30000, cfg.cppm.wave.min_sync_us, err, err_len));
    }
    if (std::strcmp(key, "cppm.sync_max") == 0) {
        return ok(assignInt(value, 3000, 40000, cfg.cppm.wave.max_sync_us, err, err_len));
    }
    if (std::strcmp(key, "failsafe.mode") == 0) {
        FailsafeMode mode;
        if (!failsafeModeFromName(value, mode)) {
            setErr(err, err_len, "expected stop|hold|preset");
            return Result::BadValue;
        }
        cfg.failsafe = mode;
        return Result::Ok;
    }
    if (std::strcmp(key, "input.timeout") == 0) {
        return ok(assignInt(value, 0, 60000, cfg.input_timeout_ms, err, err_len));
    }
    if (std::strcmp(key, "led.gpio") == 0) {
        return ok(assignInt(value, -1, 48, cfg.led_gpio, err, err_len));
    }
    if (std::strcmp(key, "log.input") == 0) {
        return ok(assignBool(value, cfg.log_input, err, err_len));
    }
    if (std::strcmp(key, "ffb.enabled") == 0) {
        return ok(assignBool(value, cfg.ffb.enabled, err, err_len));
    }
    if (std::strcmp(key, "ffb.spring") == 0) {
        return ok(assignInt(value, 0, 100, cfg.ffb.spring_pct, err, err_len));
    }
    if (std::strcmp(key, "ffb.gain") == 0) {
        return ok(assignInt(value, 0, 100, cfg.ffb.gain_pct, err, err_len));
    }
    if (std::strcmp(key, "ffb.deadband") == 0) {
        return ok(assignInt(value, 0, 50, cfg.ffb.deadband_pct, err, err_len));
    }
    setErr(err, err_len, "unknown key");
    return Result::UnknownKey;
}

bool get(const Config& cfg, const char* key, char* out, std::size_t out_len) {
    if (key == nullptr || out == nullptr || out_len == 0) {
        return false;
    }
    std::size_t idx = 0;
    const char* field = nullptr;
    if (splitChannelKey(key, idx, field)) {
        return getChannel(cfg.mapping.ch[idx], field, out, out_len);
    }
    if (std::strcmp(key, "failsafe.mode") == 0) {
        std::snprintf(out, out_len, "%s", failsafeModeName(cfg.failsafe));
        return true;
    }
    long v = 0;
    if (std::strcmp(key, "sbus.enabled") == 0) {
        v = cfg.sbus.enabled;
    } else if (std::strcmp(key, "sbus.gpio") == 0) {
        v = cfg.sbus.gpio;
    } else if (std::strcmp(key, "sbus.period") == 0) {
        v = static_cast<long>(cfg.sbus.period_us);
    } else if (std::strcmp(key, "sbus.inverted") == 0) {
        v = cfg.sbus.inverted;
    } else if (std::strcmp(key, "sbus.flags") == 0) {
        v = cfg.sbus.failsafe_flags;
    } else if (std::strcmp(key, "cppm.enabled") == 0) {
        v = cfg.cppm.enabled;
    } else if (std::strcmp(key, "cppm.gpio") == 0) {
        v = cfg.cppm.gpio;
    } else if (std::strcmp(key, "cppm.channels") == 0) {
        v = cfg.cppm.wave.channels;
    } else if (std::strcmp(key, "cppm.frame") == 0) {
        v = static_cast<long>(cfg.cppm.wave.frame_us);
    } else if (std::strcmp(key, "cppm.pulse") == 0) {
        v = cfg.cppm.wave.pulse_us;
    } else if (std::strcmp(key, "cppm.inverted") == 0) {
        v = cfg.cppm.wave.inverted;
    } else if (std::strcmp(key, "cppm.min") == 0) {
        v = cfg.cppm.wave.min_us;
    } else if (std::strcmp(key, "cppm.max") == 0) {
        v = cfg.cppm.wave.max_us;
    } else if (std::strcmp(key, "cppm.sync_min") == 0) {
        v = static_cast<long>(cfg.cppm.wave.min_sync_us);
    } else if (std::strcmp(key, "cppm.sync_max") == 0) {
        v = static_cast<long>(cfg.cppm.wave.max_sync_us);
    } else if (std::strcmp(key, "input.timeout") == 0) {
        v = static_cast<long>(cfg.input_timeout_ms);
    } else if (std::strcmp(key, "led.gpio") == 0) {
        v = cfg.led_gpio;
    } else if (std::strcmp(key, "log.input") == 0) {
        v = cfg.log_input;
    } else if (std::strcmp(key, "ffb.enabled") == 0) {
        v = cfg.ffb.enabled;
    } else if (std::strcmp(key, "ffb.spring") == 0) {
        v = cfg.ffb.spring_pct;
    } else if (std::strcmp(key, "ffb.gain") == 0) {
        v = cfg.ffb.gain_pct;
    } else if (std::strcmp(key, "ffb.deadband") == 0) {
        v = cfg.ffb.deadband_pct;
    } else {
        return false;
    }
    std::snprintf(out, out_len, "%ld", v);
    return true;
}

void forEach(const Config& cfg, const char* prefix, Visitor visitor, void* ctx) {
    char value[32];
    const std::size_t plen = prefix ? std::strlen(prefix) : 0;
    auto matches = [&](const char* key) { return plen == 0 || std::strncmp(key, prefix, plen) == 0; };

    for (std::size_t i = 0; i < kGlobalKeyCount; ++i) {
        const char* key = kGlobalKeys[i];
        if (matches(key) && get(cfg, key, value, sizeof(value))) {
            visitor(key, value, ctx);
        }
    }
    for (std::size_t ch = 0; ch < kMaxChannels; ++ch) {
        for (const char* field : kChannelFields) {
            char key[24];
            std::snprintf(key, sizeof(key), "ch%u.%s", static_cast<unsigned>(ch + 1), field);
            if (matches(key) && get(cfg, key, value, sizeof(value))) {
                visitor(key, value, ctx);
            }
        }
    }
}

}  // namespace rc::kv
