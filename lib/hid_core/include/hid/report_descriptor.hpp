// SPDX-License-Identifier: MIT
// Minimal, allocation-free USB HID report descriptor parser.
// It extracts the layout of every INPUT, OUTPUT and FEATURE field (bit offset/size, usage,
// logical range, enclosing collection) so that raw reports can be decoded and built
// generically: joystick input on one side, PID force feedback output on the other.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace hid {

// Usage pages and usages relevant to joysticks (HID Usage Tables 1.x).
constexpr uint16_t kPageGenericDesktop = 0x01;
constexpr uint16_t kPageSimulation = 0x02;
constexpr uint16_t kPageButton = 0x09;
constexpr uint16_t kPageOrdinal = 0x0A;
constexpr uint16_t kPagePid = 0x0F;

constexpr uint16_t kUsageJoystick = 0x04;
constexpr uint16_t kUsageGamepad = 0x05;
constexpr uint16_t kUsageMultiAxis = 0x08;
constexpr uint16_t kUsageX = 0x30;
constexpr uint16_t kUsageY = 0x31;
constexpr uint16_t kUsageZ = 0x32;
constexpr uint16_t kUsageRx = 0x33;
constexpr uint16_t kUsageRy = 0x34;
constexpr uint16_t kUsageRz = 0x35;
constexpr uint16_t kUsageSlider = 0x36;
constexpr uint16_t kUsageDial = 0x37;
constexpr uint16_t kUsageWheel = 0x38;
constexpr uint16_t kUsageHatSwitch = 0x39;
constexpr uint16_t kUsageSimRudder = 0xBA;
constexpr uint16_t kUsageSimThrottle = 0xBB;
constexpr uint16_t kUsageSimAccelerator = 0xC4;
constexpr uint16_t kUsageSimBrake = 0xC5;

enum class ReportType : uint8_t { Input = 0, Output = 1, Feature = 2 };

const char* reportTypeName(ReportType type);

/// One report field (a "Report Count" run of identical variable items is expanded into
/// individual fields; array items are kept as a single field with `count` elements).
struct ReportField {
    uint8_t report_id = 0;  ///< 0 when the device does not use report IDs.
    uint16_t usage_page = 0;
    uint16_t usage = 0;       ///< For variable fields. For array fields: usage minimum.
    uint16_t usage_max = 0;   ///< For array fields only.
    uint16_t bit_offset = 0;  ///< Offset in bits from the start of the report payload
                              ///< (after the report ID byte when IDs are used).
    uint8_t bit_size = 0;     ///< Size of one element.
    uint8_t count = 1;        ///< Number of elements (array fields only, 1 otherwise).
    int32_t logical_min = 0;
    int32_t logical_max = 0;
    bool is_array = false;
    bool is_relative = false;
    /// True when the item declares a Null state (an out-of-range value means "no value").
    bool has_null_state = false;
    /// HID Unit and Unit Exponent of the field (0 = none). PID time fields use seconds with
    /// a negative exponent (-3 = milliseconds).
    uint32_t unit = 0;
    int8_t unit_exponent = 0;
    /// Usage of the innermost collection containing the field (0 at top level), e.g. the
    /// PID "Axes Enable" or "Direction" collections.
    uint16_t collection_usage = 0;
    uint16_t collection_page = 0;
    /// Array fields declared with an explicit usage list (not a min/max range): the list
    /// lives in ParsedDescriptor::usage_pool[usage_list_start .. +usage_list_count).
    uint16_t usage_list_start = 0;
    uint8_t usage_list_count = 0;

    /// Total size of the field in bits (all elements).
    uint32_t totalBits() const { return static_cast<uint32_t>(bit_size) * count; }
};

/// Kept for the joystick decoder API.
using InputField = ReportField;

constexpr std::size_t kMaxInputFields = 64;
constexpr std::size_t kMaxOutputFields = 160;
constexpr std::size_t kMaxFeatureFields = 48;
constexpr std::size_t kMaxReportSizes = 48;
constexpr std::size_t kUsagePoolSize = 192;

struct ReportSizeEntry {
    ReportType type = ReportType::Input;
    uint8_t report_id = 0;
    uint16_t bits = 0;
};

struct ParsedDescriptor {
    std::array<ReportField, kMaxInputFields> fields{};  ///< Input fields.
    std::size_t field_count = 0;
    std::array<ReportField, kMaxOutputFields> out_fields{};
    std::size_t out_field_count = 0;
    std::array<ReportField, kMaxFeatureFields> feature_fields{};
    std::size_t feature_field_count = 0;

    bool uses_report_ids = false;
    /// Usage of the top-level application collection (Joystick, Gamepad, ...), first one found.
    uint16_t application_usage_page = 0;
    uint16_t application_usage = 0;
    /// Payload size (in bits, excluding the report ID byte) of every report seen.
    std::array<ReportSizeEntry, kMaxReportSizes> report_sizes{};
    std::size_t report_size_count = 0;
    /// Usage lists of array fields (see ReportField::usage_list_start).
    std::array<uint16_t, kUsagePoolSize> usage_pool{};
    std::size_t usage_pool_count = 0;

    /// Payload length in bytes for a given report (0 when unknown). Input by default.
    std::size_t reportBytes(uint8_t report_id) const { return reportBytes(ReportType::Input, report_id); }
    std::size_t reportBytes(ReportType type, uint8_t report_id) const;

    /// Fields of a given type.
    const ReportField* fieldsOf(ReportType type) const;
    std::size_t fieldCountOf(ReportType type) const;

    /// Find the n-th (0-based) field of `type` with the given page/usage, optionally restricted
    /// to one report ID (report_id < 0 = any). Returns nullptr when not found.
    const ReportField* find(ReportType type, uint16_t usage_page, uint16_t usage, int report_id = -1,
                            std::size_t nth = 0) const;

    /// For an array field: the index (logical value) that selects `usage`, or -1 if the usage
    /// is not in the field's usage list/range.
    int32_t arrayValueFor(const ReportField& field, uint16_t usage) const;
};

enum class ParseResult {
    Ok,
    Truncated,      ///< Descriptor ended in the middle of an item.
    TooManyFields,  ///< More fields than the fixed tables hold (descriptor was cut short).
    StackOverflow,  ///< Push/Pop nesting deeper than supported.
    NoInputFields,  ///< Nothing usable found.
};

const char* parseResultName(ParseResult r);

/// Parse a raw report descriptor. Never allocates. Fields marked "Constant" (padding)
/// are skipped but still advance the bit offset.
ParseResult parse(const uint8_t* desc, std::size_t len, ParsedDescriptor& out);

}  // namespace hid
