// SPDX-License-Identifier: MIT
#include "rc/pulse_pack.hpp"

namespace rc::pulse {

namespace {

// Bounded stack of halves feeding symbol pairs.
class HalfWriter {
public:
    explicit HalfWriter(Packed& out) : out_(out) {}

    bool push(bool level, uint32_t ticks) {
        while (ticks > 0) {
            uint32_t chunk = ticks > kMaxTicksPerHalf ? kMaxTicksPerHalf : ticks;
            // Avoid leaving a tiny remainder (e.g. 1 tick) that some receivers may glitch on:
            // split evenly instead when the remainder would be shorter than 16 ticks.
            if (ticks > kMaxTicksPerHalf && (ticks - chunk) < 16) {
                chunk = ticks / 2;
            }
            if (!emit(level, static_cast<uint16_t>(chunk))) {
                return false;
            }
            ticks -= chunk;
        }
        return true;
    }

    bool finish(bool idle_level) {
        if (pending_) {
            return emit(idle_level, 1);
        }
        return true;
    }

private:
    bool emit(bool level, uint16_t ticks) {
        if (pending_) {
            out_.symbols[out_.count].second = Half{ticks, level};
            ++out_.count;
            pending_ = false;
            return true;
        }
        if (out_.count >= kMaxSymbols) {
            return false;
        }
        out_.symbols[out_.count].first = Half{ticks, level};
        pending_ = true;
        return true;
    }

    Packed& out_;
    bool pending_ = false;
};

}  // namespace

bool pack(const cppm::Waveform& wf, uint32_t ticks_per_us, Packed& out) {
    out.count = 0;
    if (ticks_per_us == 0) {
        return false;
    }
    HalfWriter writer(out);
    for (std::size_t i = 0; i < wf.count; ++i) {
        const cppm::Segment& seg = wf.segments[i];
        if (!writer.push(seg.level, seg.us * ticks_per_us)) {
            return false;
        }
    }
    return writer.finish(wf.idle_level);
}

}  // namespace rc::pulse
