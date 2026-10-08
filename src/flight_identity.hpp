#pragma once
#include "flight_session.hpp"

namespace xhl::flight {
struct Peer {
    std::uintptr_t backend = 0;
    std::uint16_t handle = 0;
    std::uint64_t steam = 0;
    std::uint8_t state = 0;
    bool auth_enabled = false, hosting = false;
    bool same_connection(const Peer& b) const noexcept {
        return backend && backend == b.backend && handle && handle == b.handle && steam && steam == b.steam;
    }
};
class AuthProofs {
    struct Proof { Peer peer{}; std::uint64_t epoch = 1, serial = 0; };
    std::array<Proof,16> proofs_{};
    std::uintptr_t backend_ = 0;
    std::uint64_t generation_ = 1, serial_ = 0;
public:
    struct Pending { Peer peer{}; std::uint64_t generation = 0, epoch = 0; };
    Pending begin(const Peer&) noexcept;
    bool finish(const Pending&, const Peer&, bool callback_success) noexcept;
    void remove(std::uintptr_t backend, std::uint16_t handle = 0) noexcept;
    std::uint64_t serial(const Peer&) const noexcept;
};

// Read callbacks keep the candidate native layouts testable without executing
// a server. The runtime calls them only from native lifecycle/receive hooks.
using ReadMemory = bool (*)(std::uintptr_t address, void* out, std::size_t size) noexcept;
struct OwnerChain { Identity identity{}; std::uintptr_t peer_context = 0; };
enum class OwnerStage { complete, arguments, world, player, machine, session,
                        session_player, session_machine, machine_record, peer,
                        wrapper, backend, backend_type };
bool read_owner_chain(ReadMemory, std::uintptr_t server_state, std::uintptr_t image_base,
                      std::size_t slot, OwnerChain&, OwnerStage* failure = nullptr) noexcept;
bool read_peer(ReadMemory, std::uintptr_t backend, std::uint16_t handle, Peer&) noexcept;
// The query points to a temporary system context, whose first field points to
// the owning world's embedded execution state. Read only while the hook runs.
bool read_query_world(ReadMemory, std::uintptr_t query, std::uintptr_t& world) noexcept;
}
