// SPDX-License-Identifier: MIT
#include "hid/bitfield.hpp"

#include <cmath>

namespace hid::bits {

int32_t read(const uint8_t* payload, std::size_t payload_len, const ReportField& f, std::size_t element, bool& ok) {
    const uint32_t start_bit = f.bit_offset + static_cast<uint32_t>(element) * f.bit_size;
    const uint32_t end_bit = start_bit + f.bit_size;
    if (payload == nullptr || f.bit_size == 0 || f.bit_size > 32 || end_bit > payload_len * 8) {
        ok = false;
        return 0;
    }
    ok = true;
    uint32_t value = 0;
    for (uint32_t b = 0; b < f.bit_size; ++b) {
        const uint32_t bit = start_bit + b;
        if (payload[bit >> 3] & (1u << (bit & 7))) {
            value |= (1u << b);
        }
    }
    if (f.logical_min < 0 && f.bit_size < 32) {
        const uint32_t sign = 1u << (f.bit_size - 1);
        if (value & sign) {
            value |= ~((sign << 1) - 1);
        }
    }
    return static_cast<int32_t>(value);
}

bool write(uint8_t* payload, std::size_t payload_len, const ReportField& f, std::size_t element, int32_t value) {
    const uint32_t start_bit = f.bit_offset + static_cast<uint32_t>(element) * f.bit_size;
    const uint32_t end_bit = start_bit + f.bit_size;
    if (payload == nullptr || f.bit_size == 0 || f.bit_size > 32 || end_bit > payload_len * 8) {
        return false;
    }
    const uint32_t raw = static_cast<uint32_t>(value);
    for (uint32_t b = 0; b < f.bit_size; ++b) {
        const uint32_t bit = start_bit + b;
        const uint8_t mask = static_cast<uint8_t>(1u << (bit & 7));
        if (raw & (1u << b)) {
            payload[bit >> 3] |= mask;
        } else {
            payload[bit >> 3] &= static_cast<uint8_t>(~mask);
        }
    }
    return true;
}

int32_t clamp(const ReportField& f, int32_t value) {
    if (value < f.logical_min) {
        return f.logical_min;
    }
    if (value > f.logical_max) {
        return f.logical_max;
    }
    return value;
}

int32_t scale(const ReportField& f, float norm) {
    if (f.logical_min < 0) {
        if (norm < -1.0f) {
            norm = -1.0f;
        }
        if (norm > 1.0f) {
            norm = 1.0f;
        }
        const float limit = norm < 0 ? static_cast<float>(-f.logical_min) : static_cast<float>(f.logical_max);
        return clamp(f, static_cast<int32_t>(std::lround(norm * limit)));
    }
    if (norm < 0.0f) {
        norm = 0.0f;
    }
    if (norm > 1.0f) {
        norm = 1.0f;
    }
    const float span = static_cast<float>(f.logical_max - f.logical_min);
    return clamp(f, f.logical_min + static_cast<int32_t>(std::lround(norm * span)));
}

int32_t allOnes(const ReportField& f) {
    if (f.bit_size >= 32) {
        return -1;
    }
    return static_cast<int32_t>((1u << f.bit_size) - 1u);
}

}  // namespace hid::bits
