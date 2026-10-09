#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
namespace xhl::creative_flight {
// Contract only, never allocates/protects/writes executable memory or installs.
// Integration must verify the full loaded image hash + paired static verifier,
// hold an exclusive patch transaction and retain slot/bridge until all native
// callback AND bridge paths drain. Stop/disable runtime before restoring bytes;
// restore only when current bytes still equal this plan, then drain/unhook
// scoped callbacks before freeing near-slot memory or unloading this module.
struct DispatchCallPlan {
    std::uintptr_t site=0,slot=0,bridge=0;
    std::array<std::uint8_t,8> before{},after{};
    std::array<std::uint8_t,8> slot_value{};
};
inline constexpr std::array<std::uint8_t,8> dispatch_displaced{0x48,0x8b,0x45,0x38,0x0f,0xb6,0x48,0x3d};
inline std::optional<DispatchCallPlan> make_dispatch_call_plan(
        std::uintptr_t module_base,bool full_hash_and_dispatch_verified,
        std::uintptr_t site,std::uintptr_t near_slot,std::uintptr_t bridge,
        std::span<const std::uint8_t> current) {
    constexpr std::uintptr_t server_site=0x187595;
    if(!full_hash_and_dispatch_verified || !module_base || module_base>UINTPTR_MAX-server_site-26 ||
       site!=module_base+server_site || !bridge || !near_slot || near_slot%8 ||
       near_slot>UINTPTR_MAX-8 || current.size()!=8 ||
       std::memcmp(current.data(),dispatch_displaced.data(),8)!=0)return {};
    // Slot cannot overlap stolen/native dispatch bytes (8 bytes + unchanged
    // CMP/JA/table branch), nor can bridge entry be inside those bytes/slot.
    if((near_slot>=site && near_slot<site+26) ||
       (near_slot<site && site-near_slot<8) ||
       (bridge>=site && bridge<site+26) || (bridge>=near_slot && bridge-near_slot<8))return {};
    const auto next=site+6;
    std::int64_t displacement=0;
    if(near_slot>=next){
        const auto distance=near_slot-next;
        if(distance>static_cast<std::uintptr_t>(INT32_MAX))return {};
        displacement=static_cast<std::int64_t>(distance);
    }else{
        const auto distance=next-near_slot;
        if(distance>static_cast<std::uintptr_t>(INT32_MAX)+1)return {};
        displacement=-static_cast<std::int64_t>(distance);
    }
    DispatchCallPlan result{site,near_slot,bridge,dispatch_displaced,{0xff,0x15,0,0,0,0,0x90,0x90},{}};
    const auto relative=static_cast<std::int32_t>(displacement);
    std::memcpy(result.after.data()+2,&relative,4);
    std::memcpy(result.slot_value.data(),&bridge,8);
    return result;
}
} // namespace
extern "C" void creative_g_dispatch_bridge() noexcept(false);
