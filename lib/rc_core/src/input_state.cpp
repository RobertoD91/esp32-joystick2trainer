// SPDX-License-Identifier: MIT
#include "rc/input_state.hpp"

#include <cstring>

namespace rc {

namespace {
constexpr const char* kAxisNames[kAxisCount] = {"x",    "y",     "z",        "rx",     "ry",          "rz",   "slider",
                                                "dial", "wheel", "throttle", "rudder", "accelerator", "brake"};
}

const char* axisName(Axis axis) {
    auto idx = static_cast<std::size_t>(axis);
    return idx < kAxisCount ? kAxisNames[idx] : "?";
}

bool axisFromName(const char* name, Axis& out) {
    if (name == nullptr) {
        return false;
    }
    for (std::size_t i = 0; i < kAxisCount; ++i) {
        if (std::strcmp(name, kAxisNames[i]) == 0) {
            out = static_cast<Axis>(i);
            return true;
        }
    }
    return false;
}

int8_t InputState::hatX() const {
    switch (hat) {
        case 1:
        case 2:
        case 3:
            return 1;
        case 5:
        case 6:
        case 7:
            return -1;
        default:
            return 0;
    }
}

int8_t InputState::hatY() const {
    switch (hat) {
        case 7:
        case 0:
        case 1:
            return 1;
        case 3:
        case 4:
        case 5:
            return -1;
        default:
            return 0;
    }
}

}  // namespace rc
