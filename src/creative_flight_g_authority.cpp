#include "creative_flight_g_authority.hpp"
#include <charconv>
#include <limits>

namespace xhl::creative_flight {
namespace {
bool valid_owner(const flight::Identity& identity,std::uint32_t owner) noexcept {
    const auto slot=static_cast<std::size_t>(identity.player&63);
    return owner>=1&&owner<=16&&slot<16&&owner==slot+1&&identity.valid(slot)
        &&identity.lifecycle&&identity.steam&&identity.authentication&&identity.world;
}
bool parse_number(std::string_view text,int base,std::uint64_t& value) noexcept {
    if(text.empty())return false;
    value=0;
    const auto result=std::from_chars(text.data(),text.data()+text.size(),value,base);
    return result.ec==std::errc{}&&result.ptr==text.data()+text.size();
}
bool next_token(std::string_view& input,std::string_view& token) noexcept {
    const auto space=input.find(' ');
    if(space==std::string_view::npos||space==0)return false;
    token=input.substr(0,space);input.remove_prefix(space+1);
    return !input.empty()&&input.front()!=' ';
}
}
ServerGAuthority::ServerGAuthority(std::uint64_t first_generation) noexcept
    :next_generation_(first_generation?first_generation:1),
     generation_exhausted_(first_generation==0){}
bool ServerGAuthority::valid_snapshot(const ServerGSnapshot& value,bool needs_actor) noexcept {
    return valid_owner(value.identity,value.owner)&&value.player_life&&value.now_ms
        &&value.authenticated&&value.actor_alive&&value.session_valid&&value.allowlisted
        &&value.service_ready&&value.runtime_active&&(!needs_actor||value.actor);
}
void ServerGAuthority::revoke_activation(Slot& slot) noexcept {
    slot.active=false;slot.generation=0;slot.actor=0;
}
void ServerGAuthority::clear_slot(Slot& slot) noexcept {slot={};}
bool ServerGAuthority::lease_refreshed(const flight::Identity& identity,
        std::uint64_t player_life,std::uint64_t deadline_ms,std::uint64_t now_ms) noexcept {
    const auto owner=static_cast<std::size_t>((identity.player&63)+1);
    if(owner<1||owner>slots_.size()||!valid_owner(identity,static_cast<std::uint32_t>(owner))
        ||!player_life||!now_ms||deadline_ms<=now_ms)return false;
    std::lock_guard lock(mutex_);
    auto& slot=slots_[owner-1];
    if(slot.lease_valid&&now_ms<slot.last_now_ms){clear_slot(slot);return false;}
    if(slot.lease_valid&&(slot.identity!=identity||slot.player_life!=player_life))
        clear_slot(slot);
    if(slot.active&&(slot.identity!=identity||slot.player_life!=player_life))
        revoke_activation(slot);
    slot.identity=identity;slot.player_life=player_life;slot.deadline_ms=deadline_ms;
    slot.last_now_ms=now_ms;slot.lease_valid=true;
    return true;
}
bool ServerGAuthority::capability_available(const ServerGSnapshot& current) noexcept {
    if(current.owner<1||current.owner>slots_.size())return false;
    std::lock_guard lock(mutex_);
    auto& slot=slots_[current.owner-1];
    if(slot.lease_valid&&current.now_ms&&current.now_ms<slot.last_now_ms){
        clear_slot(slot);return false;
    }
    if(!valid_snapshot(current,false)){
        if(slot.active)revoke_activation(slot);
        return false;
    }
    if(!slot.lease_valid||slot.identity!=current.identity
        ||slot.player_life!=current.player_life||current.now_ms>=slot.deadline_ms){
        if(slot.active)revoke_activation(slot);
        return false;
    }
    slot.last_now_ms=current.now_ms;
    return true;
}
bool ServerGAuthority::set_creative_flight(const ServerGSnapshot& current,bool enabled,
        bool core_enabled_before) noexcept {
    if(current.owner<1||current.owner>slots_.size())return false;
    std::lock_guard lock(mutex_);
    auto& slot=slots_[current.owner-1];
    if(!enabled){
        if(slot.identity.steam==current.identity.steam
            &&slot.player_life==current.player_life)revoke_activation(slot);
        return true;
    }
    if(slot.lease_valid&&current.now_ms&&current.now_ms<slot.last_now_ms){
        clear_slot(slot);return false;
    }
    if(!valid_snapshot(current,false)||!slot.lease_valid
        ||slot.identity!=current.identity||slot.player_life!=current.player_life
        ||current.now_ms>=slot.deadline_ms)return false;
    if(core_enabled_before){
        slot.last_now_ms=current.now_ms;
        return slot.active&&slot.identity==current.identity
            &&slot.player_life==current.player_life&&slot.generation!=0;
    }
    if(generation_exhausted_)return false;
    revoke_activation(slot);
    slot.active=true;slot.actor=0;slot.generation=next_generation_;
    if(next_generation_==std::numeric_limits<std::uint64_t>::max())generation_exhausted_=true;
    else ++next_generation_;
    slot.last_now_ms=current.now_ms;
    return true;
}
std::optional<MovementApproval> ServerGAuthority::authorize(
        const ServerGSnapshot& current) noexcept {
    if(current.owner<1||current.owner>slots_.size())return {};
    std::lock_guard lock(mutex_);
    auto& slot=slots_[current.owner-1];
    if(!slot.active)return {};
    if(slot.lease_valid&&current.now_ms&&current.now_ms<slot.last_now_ms){
        clear_slot(slot);return {};
    }
    if(!valid_snapshot(current,true)||!current.core_enabled||!slot.lease_valid
        ||slot.identity!=current.identity||slot.player_life!=current.player_life
        ||current.now_ms>=slot.deadline_ms){
        revoke_activation(slot);return {};
    }
    if(slot.actor&&slot.actor!=current.actor){revoke_activation(slot);return {};}
    slot.actor=current.actor;slot.last_now_ms=current.now_ms;
    MovementApproval approval{slot.identity,slot.generation,slot.actor};
    return approval.valid()?std::optional<MovementApproval>{approval}:std::nullopt;
}
void ServerGAuthority::revoke_player(std::uint32_t owner,std::uint64_t steam,
        std::uint64_t player_life) noexcept {
    if(owner<1||owner>slots_.size())return;
    std::lock_guard lock(mutex_);
    auto& slot=slots_[owner-1];
    if(slot.identity.steam==steam&&slot.player_life==player_life)clear_slot(slot);
}
void ServerGAuthority::revoke(const flight::Identity& identity) noexcept {
    const auto owner=static_cast<std::size_t>((identity.player&63)+1);
    if(owner<1||owner>slots_.size())return;
    std::lock_guard lock(mutex_);
    auto& slot=slots_[owner-1];
    if(slot.identity==identity)clear_slot(slot);
}
void ServerGAuthority::revoke_all() noexcept {
    std::lock_guard lock(mutex_);
    for(auto& slot:slots_)clear_slot(slot);
}
std::optional<GLocalCommand> parse_g_local_command(std::string_view text) noexcept {
    if(text.size()>127)return {};
    if(text.ends_with("\r\n"))text.remove_suffix(2);
    else if(text.ends_with("\n")||text.ends_with("\r"))text.remove_suffix(1);
    if(text.empty()||text.find_first_of("\r\n\t")!=std::string_view::npos)return {};
    if(text=="status-phase-v1")return GLocalCommand{GLocalCommandKind::status,0,0,0,0};
    if(text.starts_with("observe-phase-v1 ")){
        text.remove_prefix(sizeof("observe-phase-v1 ")-1);
        std::uint64_t token=0;
        if(!parse_number(text,10,token)||!token)return {};
        return GLocalCommand{GLocalCommandKind::observe,token,0,0,0};
    }
    constexpr std::string_view prefix="accept-phase-v1 ";
    if(!text.starts_with(prefix))return {};
    text.remove_prefix(prefix.size());
    std::string_view id_text,count_text,digest_text;
    if(!next_token(text,id_text)||!next_token(text,count_text))return {};
    digest_text=text;
    std::uint64_t id=0,count=0,digest=0;
    if(!parse_number(id_text,10,id)||!id||!parse_number(count_text,10,count)
        ||!count||digest_text.size()!=16||!parse_number(digest_text,16,digest))return {};
    return GLocalCommand{GLocalCommandKind::accept,0,id,count,digest};
}
bool matches_g_phase_confirmation(const ServerPhaseEvidence& evidence,
        const GLocalCommand& command) noexcept {
    return command.kind==GLocalCommandKind::accept&&evidence.complete
        &&evidence.phase_mask==0x0f&&evidence.event_count>=16
        &&evidence.elapsed_ms>=1000&&command.observation_id==evidence.observation_id
        &&command.event_count==evidence.event_count&&command.digest==evidence.digest;
}
}