// SPDX-License-Identifier: MIT
// Bit-level read/write of report fields (little-endian, LSB first, as HID packs them).
#pragma once

#include <cstddef>
#include <cstdint>

#include "hid/report_descriptor.hpp"

namespace hid::bits {

/// Read element `element` of `field` from `payload` (report data without the report ID byte).
/// Sign-extends when the field's logical minimum is negative. `ok` is false when the field
/// does not fit in the payload.
int32_t read(const uint8_t* payload, std::size_t payload_len, const ReportField& field, std::size_t element, bool& ok);

/// Write `value` (two's complement, truncated to the field width) into element `element` of
/// `field`. Returns false when the field does not fit in the payload.
bool write(uint8_t* payload, std::size_t payload_len, const ReportField& field, std::size_t element, int32_t value);

/// Clamp a value to the field's logical range.
int32_t clamp(const ReportField& field, int32_t value);

/// Map a normalised value to the field's logical range.
/// - unsigned fields (logical_min >= 0): `norm` in 0..1 maps to min..max;
/// - signed fields: `norm` in -1..1 maps to min..max keeping 0 at 0.
int32_t scale(const ReportField& field, float norm);

/// The "all ones" value of the field, used by PID for "infinite" durations (null value).
int32_t allOnes(const ReportField& field);

}  // namespace hid::bits
