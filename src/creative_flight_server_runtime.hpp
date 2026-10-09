#pragma once
#include "creative_flight_native_movement.hpp"
#include <functional>
#include <memory>
#include <vector>

namespace xhl::creative_flight {
// AMD64 native ABI: callback/iterator/Dive/state signatures proven at pinned
// callers. These API routes install no hooks. The integration supplies each
// ORIGINAL trampoline explicitly and routes only the named native callback.
// Dispatch uses the separately assembled CALL bridge, never a fake prototype.
enum class ServerPhase : std::uint8_t { control,state,mover,gravity };
using ServerCallback=void (*)(void*);
using ServerIterator=bool (*)(void*,void*,std::uint32_t);
using ServerEntity=void* (*)(void*,void*);
using ServerState=std::uint8_t (*)(void*,void*,void*,void*,void*,void*,void*,void*);
using ServerMover=void (*)(std::uintptr_t*,void*);
struct ServerObservationBinding {
    flight::Identity identity{};
    std::uint64_t player_life=0;
    std::uintptr_t world=0,actor=0;
    std::uint32_t owner=0;
    bool valid() const noexcept {
        const auto slot=static_cast<std::size_t>(identity.player&63);
        return slot<16&&identity.valid(slot)&&identity.lifecycle&&identity.world==world
            &&owner==slot+1&&player_life&&actor;
    }
    bool operator==(const ServerObservationBinding&) const=default;
};
struct ServerGProvider {
    std::function<bool(std::uintptr_t,void*,std::size_t)> read;
    // Current distinct G approval only: full authenticated Identity, live actor,
    // allowlist/capability/lease and G activation generation checked together.
    std::function<std::optional<MovementApproval>(std::uintptr_t,std::uint32_t,std::uintptr_t)> approve;
    // Private read-only phase observation may validate a current authenticated
    // Actor without minting or requiring any G movement generation.
    std::function<std::optional<ServerObservationBinding>(std::uintptr_t,std::uint32_t,std::uintptr_t)> observe_binding;
    std::function<std::uint64_t()> now_ms;
    ServerEntity entity=nullptr;
    ServerMover dive=nullptr;
};
struct ServerGOptions {
    bool movement=false,observe_phases=false; // Both default OFF.
    bool full_pinned_server_hash_verified=false;
    // Local isolated acceptance of actual phase ordering, not a claim of a
    // simulation-frame counter or scheduler proof. Required only for movement.
    bool phase_order_accepted=false;
};
struct ServerPhaseEvent {
    ServerPhase phase=ServerPhase::control;
    std::uint64_t order=0,elapsed_ms=0;
};
// Pointer-free local summary of one bounded observer window.
struct ServerPhaseEvidence {
    std::uint64_t observation_id=0,event_count=0,digest=0,elapsed_ms=0;
    std::uint8_t phase_mask=0;
    bool complete=false,stopped=false,accepted=false;
};
class ServerGRuntime {
public:
    ServerGRuntime();
    ~ServerGRuntime();
    ServerGRuntime(const ServerGRuntime&)=delete;
    ServerGRuntime& operator=(const ServerGRuntime&)=delete;
    // Fixed callbacks/module verification remain valid until stop() drains all
    // scopes. Start creates no hook and announces no capability.
    bool start(ServerGProvider,ServerGOptions);
    // Outside adapter/native callbacks only. Returns false on a reentrant stop;
    // disables immediately, but caller must later drain before destroying it.
    bool stop();
    bool active() const;
    std::vector<ServerPhaseEvent> take_phase_events();
    ServerPhaseEvidence phase_evidence() const;
    bool reset_phase_observer();
    bool accept_phase_order(const ServerPhaseEvidence&);
    bool enable_movement();
    void callback(ServerPhase,void* query,ServerCallback original);
    bool query_step(void* query,void* row,std::uint32_t size,ServerIterator original);
    std::uint8_t state_decision(void* a,void* actor,void* c,void* d,void* e,void* f,void* g,void* h,ServerState original);
    // Only the real CALL bridge invokes this with original RBP-derived frame.
    // No original mover is supplied: native CMP/table/continuation resumes once.
    // Outside an exact retained mover scope this is a fast no-op.
    static void dispatch_current(std::uintptr_t* row,void* second);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
