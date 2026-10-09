#include "creative_flight_g_authority.hpp"
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace xhl::creative_flight;
namespace {
int checks=0;
void check(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
xhl::flight::Identity identity(std::uint64_t lifecycle=6,std::uint32_t player=64){
    return {1,2,0x60000000,player,128,64,0x0110000100000001,5,lifecycle};
}
ServerGSnapshot current(bool ready=true){
    ServerGSnapshot value;
    value.identity=identity();value.player_life=40;value.now_ms=1000;value.owner=1;
    value.authenticated=true;value.actor_alive=true;value.session_valid=true;value.allowlisted=true;
    value.service_ready=ready;value.runtime_active=ready;return value;
}
void authority_checks(){
    ServerGAuthority authority;
    auto value=current();
    check(!authority.capability_available(value),"capability stays closed before a mirrored lease");
    check(authority.lease_refreshed(value.identity,value.player_life,5000,value.now_ms),
        "current Creative session lease captured");
    check(authority.capability_available(value),"ready authenticated allowlisted lease advertises G");
    check(authority.set_creative_flight(value,true,false),"accepted off-to-on transition mints G");
    value.core_enabled=true;value.actor=0x70000000;
    const auto first=authority.authorize(value);
    check(first&&first->valid()&&first->generation==1&&first->actor==value.actor,
        "first current live Actor binds the distinct G generation");
    check(authority.set_creative_flight(value,true,true),"repeated on preserves current activation");
    const auto repeated=authority.authorize(value);
    check(repeated&&*repeated==*first,"repeated on does not mint a new generation");

    value.now_ms=2000;
    check(authority.lease_refreshed(value.identity,value.player_life,7000,value.now_ms),
        "same-life lease refresh succeeds");
    const auto renewed=authority.authorize(value);
    check(renewed&&renewed->generation==first->generation,
        "lease refresh preserves activation generation");

    value.now_ms=2100;
    check(authority.set_creative_flight(value,false,true),"off transition is accepted");
    check(!authority.authorize(value),"off revokes movement approval");
    value.core_enabled=false;value.actor=0;
    check(authority.set_creative_flight(value,true,false),"later accepted on transition succeeds");
    value.core_enabled=true;value.actor=0x70000000;
    const auto second=authority.authorize(value);
    check(second&&second->generation>first->generation,
        "fresh activation receives a strictly newer generation");

    auto changed_actor=value;changed_actor.actor=0x70001000;changed_actor.now_ms++;
    check(!authority.authorize(changed_actor),"Actor pointer replacement revokes the activation");
    check(!authority.authorize(value),"retired Actor binding cannot be reused");

    authority.revoke_all();
    value=current();value.now_ms=1000;value.core_enabled=false;
    check(authority.lease_refreshed(value.identity,value.player_life,3000,value.now_ms),
        "lease restored after explicit revoke");
    check(authority.set_creative_flight(value,true,false),"activation after revoke succeeds");
    value.core_enabled=true;value.actor=0x70000000;value.now_ms=1500;
    check(authority.authorize(value).has_value(),"current approval before expiry");
    value.now_ms=3000;
    check(!authority.authorize(value),"lease deadline is exclusive and revokes G");
    check(!authority.capability_available(value),"expired Creative lease removes capability");

    authority.revoke_all();
    value=current();authority.lease_refreshed(value.identity,value.player_life,5000,value.now_ms);
    check(authority.set_creative_flight(value,true,false),"new activation before loss tests");
    value.service_ready=false;
    check(!authority.capability_available(value),"installer/readiness loss closes capability");
    value.service_ready=true;value.runtime_active=false;
    check(!authority.capability_available(value),"runtime loss closes capability");
    value.runtime_active=true;value.session_valid=false;
    check(!authority.capability_available(value),"session revocation closes capability");
    value.session_valid=true;value.allowlisted=false;
    check(!authority.capability_available(value),"allowlist loss closes capability");

    authority.revoke_all();value=current();
    check(authority.lease_refreshed(value.identity,value.player_life,5000,value.now_ms),
        "fresh lease before identity checks");
    check(authority.set_creative_flight(value,true,false),"activation before identity check");
    auto replaced=value;replaced.identity=identity(7);replaced.now_ms++;
    check(!authority.authorize(replaced),"lifecycle identity change revokes approval");
    check(!authority.authorize(value),"old authenticated identity remains revoked");

    authority.revoke_all();value=current();
    authority.lease_refreshed(value.identity,value.player_life,5000,value.now_ms);
    check(authority.set_creative_flight(value,true,false),"activation before life change");
    check(authority.lease_refreshed(value.identity,value.player_life+1,6000,value.now_ms+1),
        "new player life refresh is recorded");
    value.player_life++;value.now_ms++;
    check(!authority.authorize(value),"player life transition retires old G activation");

    authority.revoke_all();value=current();
    authority.lease_refreshed(value.identity,value.player_life,5000,value.now_ms);
    check(authority.set_creative_flight(value,true,false),"activation before clock rollback");
    value.now_ms=999;
    check(!authority.authorize(value),"monotonic clock rollback fails closed");

    authority.revoke_all();value=current();
    authority.lease_refreshed(value.identity,value.player_life,5000,value.now_ms);
    check(authority.set_creative_flight(value,true,false),"activation before privilege loss");
    value.core_enabled=false;value.actor=0x70000000;
    check(!authority.authorize(value),"lost core Creative-flight privilege revokes G");
    value.core_enabled=true;
    check(!authority.authorize(value),"revoked privilege cannot reuse old generation");

    ServerGAuthority final_generation(std::numeric_limits<std::uint64_t>::max());
    value=current();
    check(final_generation.lease_refreshed(value.identity,value.player_life,5000,value.now_ms),
        "lease before generation exhaustion");
    check(final_generation.set_creative_flight(value,true,false),"last nonzero generation may be issued");
    value.core_enabled=true;value.actor=0x70000000;
    const auto last=final_generation.authorize(value);
    check(last&&last->generation==std::numeric_limits<std::uint64_t>::max(),
        "generation does not wrap to zero");
    final_generation.set_creative_flight(value,false,true);
    value.core_enabled=false;value.actor=0;
    check(!final_generation.set_creative_flight(value,true,false),
        "generation exhaustion fails closed");
}
void local_confirmation_checks(){
    auto status=parse_g_local_command("status-phase-v1");
    check(status&&status->kind==GLocalCommandKind::status,"local pointer-free status request parses");
    check(!parse_g_local_command("status-phase-v1 extra"),"status command rejects extra data");
    auto observe=parse_g_local_command("observe-phase-v1 19\r\n");
    check(observe&&observe->kind==GLocalCommandKind::observe&&observe->token==19,
        "local observation request parses");
    auto accept=parse_g_local_command("accept-phase-v1 19 24 0123456789abcdef\n");
    check(accept&&accept->kind==GLocalCommandKind::accept&&accept->observation_id==19
        &&accept->event_count==24&&accept->digest==0x0123456789abcdefull,
        "explicit local phase confirmation parses");
    check(!parse_g_local_command("accept-phase-v1 19 24 123"),"short digest rejected");
    check(!parse_g_local_command("accept-phase-v1 19 24 0123456789abcdef extra"),
        "extra confirmation fields rejected");
    check(!parse_g_local_command("observe-phase-v1 0"),"zero observation token rejected");
    check(!parse_g_local_command("observe-phase-v1 4\nobserve-phase-v1 5"),
        "multiple commands rejected");
    check(!parse_g_local_command(std::string(128,'x')),"oversized local command rejected");

    ServerPhaseEvidence evidence{19,24,0x0123456789abcdefull,1500,0x0f,true,false,false};
    check(matches_g_phase_confirmation(evidence,*accept),
        "only complete current bounded sample matches local acceptance");
    evidence.phase_mask=0x07;
    check(!matches_g_phase_confirmation(evidence,*accept),"missing phase prevents acceptance");
    evidence.phase_mask=0x0f;evidence.event_count=15;
    check(!matches_g_phase_confirmation(evidence,*accept),"short sample prevents acceptance");
    evidence.event_count=24;evidence.elapsed_ms=999;
    check(!matches_g_phase_confirmation(evidence,*accept),"short duration prevents acceptance");
    evidence.elapsed_ms=1500;evidence.observation_id++;
    check(!matches_g_phase_confirmation(evidence,*accept),"stale sample id prevents acceptance");
}
}
int main(){
    try{authority_checks();local_confirmation_checks();}
    catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
    std::cout<<"G authority/local phase checks: "<<checks<<'\n';return 0;
}