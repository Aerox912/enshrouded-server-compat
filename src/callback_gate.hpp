#pragma once
#include <mutex>

namespace xhl::flight {
// Callback storage outlives its registration. Disable waits for an invocation
// already inside the gate; an invocation dispatched late sees empty state.
template<class State> class CallbackGate {
    std::mutex guard_;
    State state_{};
public:
    void configure(State state) { std::lock_guard lock(guard_); state_=state; }
    void disable() { configure({}); }
    template<class F> void invoke(F action) { std::lock_guard lock(guard_); action(state_); }
};
}
