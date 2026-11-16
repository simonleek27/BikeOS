#pragma once

#include <cstdint>

namespace bikeos {

class NavigationRuntime {
public:
    bool initialize();
    void step(std::uint64_t now_ms);

private:
    struct State;
    State* state_{};
};

}  // namespace bikeos
