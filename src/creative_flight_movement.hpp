#pragma once
#include "flight_session.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace xhl::creative_flight {
inline constexpr unsigned jump_action=30,sprint_action=50,sneak_action=52;
struct Vector3 { float x=0,y=0,z=0; bool operator==(const Vector3&) const=default; };
// Server G approval only. generation changes on every new G activation; lease
// renewal keeps it. Client supplies this same approval plus its resolved local
// actor. No F6 permission may be adapted to this contract.
struct MovementApproval {
    flight::Identity identity{};
    std::uint64_t generation=0;
    std::uintptr_t actor=0;
    bool operator==(const MovementApproval&) const=default;
    bool valid() const noexcept {
        return (identity.player & 63)<16 && identity.lifecycle &&
            identity.valid(identity.player & 63) && generation && actor;
    }
};
struct ConfiguredInput { std::uint64_t raw=0,toggle=0; };
struct MovementCommand { bool climb=false,sink=false,fast=false; Vector3 target{}; std::uint64_t sample=0; };
// This value class is owned and serialized by the native per-actor adapter.
// Inputs must be copied from the current PlayerInput and native camera-relative
// movement frame; no keyboard keys, one-shot consumed flags or borrowed pointers.
class MovementPolicy {
    struct Action {
        bool initialized=false,previous=false,toggle=false,active=false;
        bool update(bool raw,bool mode) noexcept {
            if(!initialized || toggle!=mode) {
                initialized=true;previous=raw;toggle=mode;active=mode?false:raw;
                return active; // A held toggle at approval/remap is not a fresh press.
            }
            if(mode){if(raw && !previous)active=!active;}
            else active=raw;
            previous=raw;return active;
        }
    };
    std::optional<MovementApproval> binding_,retired_;
    std::array<Action,3> actions_{};
    bool flying_=false;
    std::uint64_t sample_=0;bool consumed_=true;
public:
    void revoke() noexcept {
        if(binding_)retired_=binding_;
        binding_.reset();actions_={};flying_=false;consumed_=true;
    }
    bool flying() const noexcept { return flying_; }
    bool matches(const MovementApproval& approval) const noexcept { return binding_ && *binding_==approval; }
    bool consume(const MovementApproval& expected,const MovementCommand& command) noexcept {
        if(!binding_ || *binding_!=expected || !flying_ || consumed_ || command.sample!=sample_)return false;
        consumed_=true;return true;
    }
    std::optional<MovementCommand> sample(const MovementApproval& approval,
            std::uint64_t actor_state,ConfiguredInput input,Vector3 native_world_move) noexcept {
        constexpr auto unavailable=(1ull<<7)|(1ull<<12); // Dead / Spawning
        if(!approval.valid() || (actor_state&unavailable) ||
            !std::isfinite(native_world_move.x) || !std::isfinite(native_world_move.y) ||
            !std::isfinite(native_world_move.z)) {revoke();return {};}
        if(binding_ && *binding_!=approval)revoke();
        // An actor swap with the same approval cannot transfer G. The authority
        // must issue a new G generation after death, revocation or actor change.
        auto same_life_independent=[](flight::Identity a,flight::Identity b){a.lifecycle=0;b.lifecycle=0;return a==b;};
        if(retired_ && same_life_independent(retired_->identity,approval.identity) &&
            retired_->generation==approval.generation)return {};
        if(!binding_){binding_=approval;actions_={};flying_=false;}
        if(sample_==UINT64_MAX){revoke();return {};}
        MovementCommand result;result.sample=++sample_;consumed_=false;
        constexpr std::array<unsigned,3> bits{jump_action,sneak_action,sprint_action};
        std::array<bool,3> values{};
        for(std::size_t i=0;i<bits.size();++i)values[i]=actions_[i].update(
            (input.raw&(1ull<<bits[i]))!=0,(input.toggle&(1ull<<bits[i]))!=0);
        result.climb=values[0];result.sink=values[1];result.fast=values[2];
        const float speed=result.fast?8.0f:1.5f;
        // Original Creative scaling: native camera-relative horizontal movement,
        // vertical climb-minus-sink 5, then multiply the entire vector by speed.
        result.target={native_world_move.x*speed,
            (float(result.climb)-float(result.sink))*5.0f*speed,native_world_move.z*speed};
        if(!std::isfinite(result.target.x) || !std::isfinite(result.target.z)){revoke();return {};}
        return result;
    }
    // Native state priorities remain authoritative. Only ordinary Falling is
    // replaced; grounded/attachments/water/gliding clear this flight-local run.
    std::uint8_t state(std::uint8_t native_state) noexcept {
        if(!binding_)return native_state;
        if(native_state==2){flying_=true;return 3;}
        if(native_state==3 && flying_)return 3;
        flying_=false;actions_={};return native_state;
    }
};
}
