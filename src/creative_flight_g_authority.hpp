#pragma once
#include "creative_flight_server_runtime.hpp"
#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string_view>

namespace xhl::creative_flight {
struct ServerGSnapshot {
    flight::Identity identity{};
    std::uint64_t player_life=0,now_ms=0;
    std::uintptr_t actor=0;
    std::uint32_t owner=0;
    bool authenticated=false,actor_alive=false,session_valid=false,allowlisted=false;
    bool core_enabled=false,service_ready=false,runtime_active=false;
};
class ServerGAuthority {
public:
    explicit ServerGAuthority(std::uint64_t first_generation=1) noexcept;
    bool lease_refreshed(const flight::Identity&,std::uint64_t player_life,
        std::uint64_t deadline_ms,std::uint64_t now_ms) noexcept;
    bool capability_available(const ServerGSnapshot&) noexcept;
    bool set_creative_flight(const ServerGSnapshot&,bool enabled,
        bool core_enabled_before) noexcept;
    std::optional<MovementApproval> authorize(const ServerGSnapshot&) noexcept;
    void revoke_player(std::uint32_t owner,std::uint64_t steam,
        std::uint64_t player_life) noexcept;
    void revoke(const flight::Identity&) noexcept;
    void revoke_all() noexcept;
private:
    struct Slot {
        flight::Identity identity{};
        std::uint64_t player_life=0,deadline_ms=0,last_now_ms=0,generation=0;
        std::uintptr_t actor=0;
        bool lease_valid=false,active=false;
    };
    mutable std::mutex mutex_;
    std::array<Slot,16> slots_{};
    std::uint64_t next_generation_=1;
    bool generation_exhausted_=false;
    static bool valid_snapshot(const ServerGSnapshot&,bool needs_actor) noexcept;
    static void revoke_activation(Slot&) noexcept;
    void clear_slot(Slot&) noexcept;
};

enum class GLocalCommandKind : std::uint8_t { observe,accept,status };
struct GLocalCommand {
    GLocalCommandKind kind=GLocalCommandKind::observe;
    std::uint64_t token=0;
    std::uint64_t observation_id=0,event_count=0,digest=0;
};
std::optional<GLocalCommand> parse_g_local_command(std::string_view) noexcept;
bool matches_g_phase_confirmation(const ServerPhaseEvidence&,
    const GLocalCommand&) noexcept;
}