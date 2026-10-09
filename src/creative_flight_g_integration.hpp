#pragma once
#include "creative_flight_call_service.hpp"
#include "creative_flight_server_runtime.hpp"
#include <cstddef>
#include <span>
namespace xhl::creative_flight {
using GImageRead = bool (*)(void*,std::uintptr_t,void*,std::size_t) noexcept;
// Exact loaded entry/call-site bytes captured from the full-hash pinned server
// image and cross-checked against the accepted G server ABI/static reports.
bool verify_server_g_runtime_anchors(std::uintptr_t base,GImageRead,void*) noexcept;
bool verify_server_g_bridge_prologue(std::uintptr_t bridge,GImageRead,void*) noexcept;
// Called under the existing authority/provider lock. Authorize must resolve
// the current authenticated Identity, live actor, allowlist, capability lease
// and DISTINCT G generation. This gate cannot mint any authority or generation.
template<class Authorize>
std::optional<MovementApproval> gated_server_g_approval(
        const call_install::InstallService& service,bool runtime_active,
        std::uintptr_t world,std::uint32_t owner,std::uintptr_t actor,Authorize&& authorize) {
    if(!runtime_active||!service.ready(call_install::Image::server)||!world||!actor||owner<1||owner>16)return {};
    auto value=authorize(world,owner,actor);
    if(!value||!value->valid()||value->identity.world!=world||value->actor!=actor||
        (value->identity.player&63)+1!=owner||!service.ready(call_install::Image::server))return {};
    return value;
}
}
