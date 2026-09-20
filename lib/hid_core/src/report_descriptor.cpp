// SPDX-License-Identifier: MIT
#include "hid/report_descriptor.hpp"

namespace hid {

namespace {

// Item prefix decoding (HID 1.11, section 6.2.2.2).
constexpr uint8_t kTypeMain = 0;
constexpr uint8_t kTypeGlobal = 1;
constexpr uint8_t kTypeLocal = 2;

constexpr uint8_t kMainInput = 0x8;
constexpr uint8_t kMainCollection = 0xA;
constexpr uint8_t kMainEndCollection = 0xC;

constexpr uint8_t kGlobalUsagePage = 0x0;
constexpr uint8_t kGlobalLogicalMin = 0x1;
constexpr uint8_t kGlobalLogicalMax = 0x2;
constexpr uint8_t kGlobalReportSize = 0x7;
constexpr uint8_t kGlobalReportId = 0x8;
constexpr uint8_t kGlobalReportCount = 0x9;
constexpr uint8_t kGlobalPush = 0xA;
constexpr uint8_t kGlobalPop = 0xB;

constexpr uint8_t kLocalUsage = 0x0;
constexpr uint8_t kLocalUsageMin = 0x1;
constexpr uint8_t kLocalUsageMax = 0x2;

constexpr uint8_t kCollectionApplication = 0x01;

constexpr std::size_t kMaxLocalUsages = 32;
constexpr std::size_t kMaxGlobalStack = 4;

struct GlobalState {
    uint16_t usage_page = 0;
    int32_t logical_min = 0;
    int32_t logical_max = 0;
    uint32_t report_size = 0;
    uint32_t report_count = 0;
    uint8_t report_id = 0;
};

struct LocalState {
    std::array<uint16_t, kMaxLocalUsages> usages{};
    std::array<uint16_t, kMaxLocalUsages> usage_pages{};  // 0 = use the global page
    std::size_t usage_count = 0;
    uint16_t usage_min = 0;
    uint16_t usage_max = 0;
    uint16_t usage_min_page = 0;
    bool has_range = false;

    void clear() { *this = LocalState{}; }
};

int32_t readSigned(const uint8_t* p, std::size_t size) {
    switch (size) {
        case 1:
            return static_cast<int8_t>(p[0]);
        case 2:
            return static_cast<int16_t>(static_cast<uint16_t>(p[0] | (p[1] << 8)));
        case 4:
            return static_cast<int32_t>(static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                                        (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24));
        default:
            return 0;
    }
}

uint32_t readUnsigned(const uint8_t* p, std::size_t size) {
    uint32_t v = 0;
    for (std::size_t i = 0; i < size; ++i) {
        v |= static_cast<uint32_t>(p[i]) << (8 * i);
    }
    return v;
}

class BitCursor {
public:
    uint16_t& offsetFor(uint8_t report_id) {
        for (std::size_t i = 0; i < count_; ++i) {
            if (ids_[i] == report_id) {
                return offsets_[i];
            }
        }
        if (count_ < kMaxReportIds) {
            ids_[count_] = report_id;
            offsets_[count_] = 0;
            return offsets_[count_++];
        }
        return overflow_;
    }

    void exportTo(ParsedDescriptor& out) const {
        out.report_size_count = count_;
        for (std::size_t i = 0; i < count_; ++i) {
            out.report_sizes[i] = ReportSizeEntry{ids_[i], offsets_[i]};
        }
    }

private:
    std::array<uint8_t, kMaxReportIds> ids_{};
    std::array<uint16_t, kMaxReportIds> offsets_{};
    std::size_t count_ = 0;
    uint16_t overflow_ = 0;
};

}  // namespace

std::size_t ParsedDescriptor::reportBytes(uint8_t report_id) const {
    for (std::size_t i = 0; i < report_size_count; ++i) {
        if (report_sizes[i].report_id == report_id) {
            return (report_sizes[i].bits + 7) / 8;
        }
    }
    return 0;
}

const char* parseResultName(ParseResult r) {
    switch (r) {
        case ParseResult::Ok:
            return "ok";
        case ParseResult::Truncated:
            return "truncated";
        case ParseResult::TooManyFields:
            return "too many fields";
        case ParseResult::StackOverflow:
            return "push/pop overflow";
        case ParseResult::NoInputFields:
            return "no input fields";
    }
    return "?";
}

ParseResult parse(const uint8_t* desc, std::size_t len, ParsedDescriptor& out) {
    out = ParsedDescriptor{};
    if (desc == nullptr) {
        return ParseResult::NoInputFields;
    }

    GlobalState g;
    LocalState l;
    std::array<GlobalState, kMaxGlobalStack> stack{};
    std::size_t stack_depth = 0;
    BitCursor cursor;
    int collection_depth = 0;

    std::size_t pos = 0;
    while (pos < len) {
        const uint8_t prefix = desc[pos++];
        if (prefix == 0xFE) {  // long item: skip
            if (pos + 2 > len) {
                return ParseResult::Truncated;
            }
            const std::size_t data_size = desc[pos];
            pos += 2 + data_size;
            if (pos > len) {
                return ParseResult::Truncated;
            }
            continue;
        }
        std::size_t size = prefix & 0x03;
        if (size == 3) {
            size = 4;
        }
        const uint8_t type = (prefix >> 2) & 0x03;
        const uint8_t tag = (prefix >> 4) & 0x0F;
        if (pos + size > len) {
            return ParseResult::Truncated;
        }
        const uint8_t* data = desc + pos;
        pos += size;
        const uint32_t udata = readUnsigned(data, size);
        const int32_t sdata = size == 0 ? 0 : readSigned(data, size);

        switch (type) {
            case kTypeGlobal:
                switch (tag) {
                    case kGlobalUsagePage:
                        g.usage_page = static_cast<uint16_t>(udata);
                        break;
                    case kGlobalLogicalMin:
                        g.logical_min = sdata;
                        break;
                    case kGlobalLogicalMax:
                        g.logical_max = sdata;
                        break;
                    case kGlobalReportSize:
                        g.report_size = udata;
                        break;
                    case kGlobalReportId:
                        g.report_id = static_cast<uint8_t>(udata);
                        out.uses_report_ids = true;
                        break;
                    case kGlobalReportCount:
                        g.report_count = udata;
                        break;
                    case kGlobalPush:
                        if (stack_depth >= kMaxGlobalStack) {
                            return ParseResult::StackOverflow;
                        }
                        stack[stack_depth++] = g;
                        break;
                    case kGlobalPop:
                        if (stack_depth == 0) {
                            return ParseResult::StackOverflow;
                        }
                        g = stack[--stack_depth];
                        break;
                    default:
                        break;  // physical min/max, unit, unit exponent: not needed
                }
                break;

            case kTypeLocal:
                switch (tag) {
                    case kLocalUsage:
                        if (l.usage_count < kMaxLocalUsages) {
                            l.usages[l.usage_count] = static_cast<uint16_t>(udata & 0xFFFF);
                            l.usage_pages[l.usage_count] = size == 4 ? static_cast<uint16_t>(udata >> 16) : 0;
                            ++l.usage_count;
                        }
                        break;
                    case kLocalUsageMin:
                        l.usage_min = static_cast<uint16_t>(udata & 0xFFFF);
                        l.usage_min_page = size == 4 ? static_cast<uint16_t>(udata >> 16) : 0;
                        l.has_range = true;
                        break;
                    case kLocalUsageMax:
                        l.usage_max = static_cast<uint16_t>(udata & 0xFFFF);
                        l.has_range = true;
                        break;
                    default:
                        break;  // designators, strings, delimiters: ignored
                }
                break;

            case kTypeMain: {
                if (tag == kMainCollection) {
                    if (collection_depth == 0 && udata == kCollectionApplication && out.application_usage == 0 &&
                        l.usage_count > 0) {
                        out.application_usage_page = l.usage_pages[0] ? l.usage_pages[0] : g.usage_page;
                        out.application_usage = l.usages[0];
                    }
                    ++collection_depth;
                } else if (tag == kMainEndCollection) {
                    if (collection_depth > 0) {
                        --collection_depth;
                    }
                } else if (tag == kMainInput) {
                    const bool is_constant = (udata & 0x01) != 0;
                    const bool is_variable = (udata & 0x02) != 0;
                    const bool is_relative = (udata & 0x04) != 0;
                    uint16_t& bit_offset = cursor.offsetFor(g.report_id);
                    const uint32_t total_bits = g.report_size * g.report_count;

                    if (is_constant || g.report_size == 0 || g.report_count == 0) {
                        bit_offset = static_cast<uint16_t>(bit_offset + total_bits);
                    } else if (is_variable) {
                        for (uint32_t i = 0; i < g.report_count; ++i) {
                            // Resolve the usage of element i: explicit list, then range, then repeat last.
                            uint16_t usage = 0;
                            uint16_t page = g.usage_page;
                            bool have = false;
                            if (i < l.usage_count) {
                                usage = l.usages[i];
                                if (l.usage_pages[i]) {
                                    page = l.usage_pages[i];
                                }
                                have = true;
                            } else if (l.has_range) {
                                uint32_t idx = i - l.usage_count;
                                if (l.usage_min + idx <= l.usage_max) {
                                    usage = static_cast<uint16_t>(l.usage_min + idx);
                                    if (l.usage_min_page) {
                                        page = l.usage_min_page;
                                    }
                                    have = true;
                                }
                            }
                            if (!have && l.usage_count > 0) {
                                usage = l.usages[l.usage_count - 1];
                                if (l.usage_pages[l.usage_count - 1]) {
                                    page = l.usage_pages[l.usage_count - 1];
                                }
                                have = true;
                            }
                            if (have) {
                                if (out.field_count >= kMaxInputFields) {
                                    return ParseResult::TooManyFields;
                                }
                                InputField& f = out.fields[out.field_count++];
                                f.report_id = g.report_id;
                                f.usage_page = page;
                                f.usage = usage;
                                f.usage_max = usage;
                                f.bit_offset = bit_offset;
                                f.bit_size = static_cast<uint8_t>(g.report_size);
                                f.count = 1;
                                f.logical_min = g.logical_min;
                                f.logical_max = g.logical_max;
                                f.is_array = false;
                                f.is_relative = is_relative;
                            }
                            bit_offset = static_cast<uint16_t>(bit_offset + g.report_size);
                        }
                    } else {
                        // Array item: each element carries an index into the usage range.
                        if (out.field_count >= kMaxInputFields) {
                            return ParseResult::TooManyFields;
                        }
                        InputField& f = out.fields[out.field_count++];
                        f.report_id = g.report_id;
                        f.usage_page = l.usage_min_page ? l.usage_min_page : g.usage_page;
                        if (l.has_range) {
                            f.usage = l.usage_min;
                            f.usage_max = l.usage_max;
                        } else if (l.usage_count > 0) {
                            f.usage = l.usages[0];
                            f.usage_max = l.usages[l.usage_count - 1];
                        }
                        f.bit_offset = bit_offset;
                        f.bit_size = static_cast<uint8_t>(g.report_size);
                        f.count = static_cast<uint8_t>(g.report_count > 255 ? 255 : g.report_count);
                        f.logical_min = g.logical_min;
                        f.logical_max = g.logical_max;
                        f.is_array = true;
                        f.is_relative = is_relative;
                        bit_offset = static_cast<uint16_t>(bit_offset + total_bits);
                    }
                } else {
                    // Output / Feature items: not decoded, but they still consume local state.
                }
                l.clear();  // locals reset after every main item
                break;
            }

            default:
                break;  // reserved type
        }
    }

    cursor.exportTo(out);
    return out.field_count == 0 ? ParseResult::NoInputFields : ParseResult::Ok;
}

}  // namespace hid
