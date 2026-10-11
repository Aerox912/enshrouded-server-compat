#pragma once
#include "creative_flight_movement.hpp"
#include <cstring>
#include <span>

namespace xhl::creative_flight {
// Copied current-frame contract. Native integration must prove these pointers
// from the actual query reader, never component registration order. The input
// read runs after native player_control_locomotion produced desiredWorldMove.
struct MovementInputFrame {
    MovementApproval approval{};
    std::uintptr_t player_input=0,actor_input=0;
};
template<class Read,class T>
bool movement_read(Read& read,std::uintptr_t pointer,std::uintptr_t offset,T& result) {
    if(!pointer || offset>UINTPTR_MAX-pointer || sizeof(T)>UINTPTR_MAX-pointer-offset)return false;
    return read(pointer+offset,&result,sizeof(result));
}
// Authorize returns the CURRENT distinct G approval, with full identity/live
// actor checks, or empty. It must not return an F6 lease or cache stale approval.
template<class Authorize,class Read>
std::optional<MovementCommand> read_movement_input(MovementPolicy& policy,
        const MovementInputFrame& frame,Authorize&& authorize,Read&& read) {
    const auto before=authorize();
    if(!before || *before!=frame.approval || !before->valid()){policy.revoke();return {};}
    ConfiguredInput input;Vector3 move;std::uint64_t state=0,state_after=0;
    if(!movement_read(read,frame.approval.actor,0xbf8,state) ||
        !movement_read(read,frame.player_input,0x320,input.raw) ||
        !movement_read(read,frame.player_input,0x510,input.toggle) ||
        !movement_read(read,frame.actor_input,0x198,move) ||
        !movement_read(read,frame.approval.actor,0xbf8,state_after)) {policy.revoke();return {};}
    const auto after=authorize();
    if(!after || *after!=*before || state_after!=state){policy.revoke();return {};}
    return policy.sample(*after,state,input,move);
}

// Call after forwarding the native eight-argument state decision unchanged.
// Only a current sampled G binding may replace ordinary Falling. Each native
// decision rechecks live Actor bits and current approval; no cached state alone
// can select flight.
template<class Authorize,class Read>
std::uint8_t select_g_state(MovementPolicy& policy,const MovementApproval& expected,
        std::uint8_t original,Authorize&& authorize,Read&& read) {
    const auto before=authorize();std::uint64_t actor_state=0;
    if(!before || *before!=expected || !before->valid() || !policy.matches(expected) ||
        !movement_read(read,expected.actor,0xbf8,actor_state) || (actor_state&((1ull<<7)|(1ull<<12)))){
        policy.revoke();return original;
    }
    const auto after=authorize();
    if(!after || *after!=expected){policy.revoke();return original;}
    return policy.state(original);
}
// Pinned paired locomotion_execution row layout comes from actual mover loads.
// Use only inside that native callback, with all 0x138 bytes/counters supplied by
// the native iterator. This helper installs no hook and fabricates no ECS row.
using MoverRow=std::span<std::uintptr_t,0x138/8>;
struct DiveScratch {
    alignas(16) std::array<unsigned char,0x1c0> config{};
    Vector3 target{};
};
// Invoke is the proven paired native Dive mover forwarded with this same row
// and the original second argument. Caller has already selected G Flying for
// this exact actor/frame; callback ordering and full detour ABI remain an
// integration gate. Entry approval linearizes this native step; lifecycle is
// checked again afterward so no local state survives concurrent revocation.
template<class Authorize,class Read,class Invoke>
bool invoke_g_dive(MovementPolicy& policy,const MovementApproval& expected,
        const MovementCommand& command,MoverRow row,void* original_second,
        Authorize&& authorize,Read&& read,Invoke&& invoke) {
    const auto before=authorize();
    if(!before || *before!=expected || !before->valid() || !policy.flying() ||
        row[0x68/8]!=expected.actor || !row[0x10/8] || !row[0x98/8]) {policy.revoke();return false;}
    DiveScratch scratch;std::uint8_t native_state=0;std::uint64_t actor_state=0;
    if(!movement_read(read,row[0x98/8],0x3d,native_state) || native_state!=3 ||
        !movement_read(read,expected.actor,0xbf8,actor_state) || (actor_state&((1ull<<7)|(1ull<<12))) ||
        !movement_read(read,row[0x10/8],0,scratch.config)) {policy.revoke();return false;}
    if(!std::isfinite(command.target.x) || !std::isfinite(command.target.y) || !std::isfinite(command.target.z)){
        policy.revoke();return false;
    }
    scratch.target=command.target;
    constexpr float acceleration=40;
    std::memcpy(scratch.config.data()+0x13c,&acceleration,4);
    std::memcpy(scratch.config.data()+0x140,&acceleration,4);
    std::uint64_t final_actor_state=0;
    if(!movement_read(read,expected.actor,0xbf8,final_actor_state) || final_actor_state!=actor_state){policy.revoke();return false;}
    const auto current=authorize();
    if(!current || *current!=expected){policy.revoke();return false;}
    if(!policy.consume(expected,command))return false;
    struct Restore {
        MoverRow row;std::uintptr_t config,target;
        ~Restore(){row[0x10/8]=config;row[0x98/8]=target;}
    } restore{row,row[0x10/8],row[0x98/8]};
    row[0x10/8]=reinterpret_cast<std::uintptr_t>(scratch.config.data());
    row[0x98/8]=reinterpret_cast<std::uintptr_t>(&scratch.target);
    try {invoke(row.data(),original_second);}catch(...){policy.revoke();throw;}
    const auto after=authorize();
    if(!after || *after!=expected)policy.revoke();
    return true;
}
}
