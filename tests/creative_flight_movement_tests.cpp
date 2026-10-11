#include "creative_flight_movement.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace xhl::creative_flight;
namespace {
int checks=0;
void check(bool v,const char* why){++checks;if(!v)throw std::runtime_error(why);}
MovementApproval approval(){ MovementApproval a; a.identity={1,2,3,64,128,64,0x0110000100000001,5,6};a.generation=7;a.actor=0x1000;return a; }
constexpr auto jump=1ull<<jump_action,sneak=1ull<<sneak_action,sprint=1ull<<sprint_action;
}
int main(){try{
    const auto a=approval();const Vector3 move{0.25f,0.9f,-0.5f};
    {MovementPolicy p;check(!p.sample({},0,{},move),"no approved identity");check(p.state(2)==2,"unauthorized state unchanged");}
    {MovementPolicy p;auto r=p.sample(a,0,{jump,0},move);check(r && r->climb,"held Jump starts climbing");
     check(r->target==Vector3{0.375f,7.5f,-0.75f},"normal original scale and camera-relative horizontal vector");
     check(p.state(2)==3 && p.flying(),"falling selects flight");
     r=p.sample(a,0,{jump,0},move);check(r && r->climb,"held Jump survives normal jump consumption");
     check(p.state(3)==3 && p.flying(),"active Flying continuation");
     r=p.sample(a,0,{jump|sneak|sprint,0},move);check(r && r->fast && r->target==Vector3{2,0,-4},"opposing vertical input cancels with fast scale");
     r=p.sample(a,0,{sneak,0},move);check(r && r->target.y==-7.5f,"sink scale");
     r=p.sample(a,0,{jump|sprint,0},move);check(r && r->target.y==40,"fast climb original scale");
     r=p.sample(a,0,{},move);check(r && !r->climb && !r->fast && r->target.y==0,"hold release clears");
     check(p.state(0)==0 && !p.flying(),"landing clears flight run");}
    for(auto bit:{jump,sneak,sprint}){MovementPolicy p;
        auto r=p.sample(a,0,{bit,bit},move);check(r && !r->climb && !r->sink && !r->fast,"held preapproval toggle not replayed");
        p.sample(a,0,{0,bit},move);r=p.sample(a,0,{bit,bit},move);
        check(r && (r->climb||r->sink||r->fast),"fresh configured toggle rising edge activates");
        r=p.sample(a,0,{bit,bit},move);check(r && (r->climb||r->sink||r->fast),"held toggle does not oscillate");
        p.sample(a,0,{0,bit},move);r=p.sample(a,0,{bit,bit},move);check(r && !r->climb && !r->sink && !r->fast,"second toggle press disables");}
    {MovementPolicy p;p.sample(a,0,{jump,0},move);auto r=p.sample(a,0,{jump,jump},move);check(r && !r->climb,"hold-to-toggle remap does not replay press");
     r=p.sample(a,0,{jump,0},move);check(r && r->climb,"toggle-to-hold reflects current input");}
    for(auto state:{1ull<<7,1ull<<12}){MovementPolicy p;p.sample(a,0,{jump,0},move);p.state(2);
        check(!p.sample(a,state,{jump,0},move) && !p.flying(),"death/spawning clears all local state");
        check(!p.sample(a,0,{jump,0},move),"same G approval cannot revive after death");
        auto fresh=a;++fresh.generation;check(bool(p.sample(fresh,0,{jump,0},move)),"fresh G activation may resume");}
    {MovementPolicy p;p.sample(a,0,{jump,0},move);p.revoke();check(!p.sample(a,0,{jump,0},move),"revoked G generation cannot resume");
     auto fresh=a;++fresh.generation;check(bool(p.sample(fresh,0,{jump,0},move)),"new G approval accepted");}
    {MovementPolicy p;p.sample(a,0,{},move);auto swapped=a;++swapped.actor;check(!p.sample(swapped,0,{},move),"actor change cannot inherit G generation");}
    for(unsigned field=0;field<10;++field){MovementPolicy p;p.sample(a,0,{jump,0},move);auto changed=a;
        switch(field){case 0:++changed.identity.backend;break;case 1:++changed.identity.session;break;case 2:++changed.identity.world;break;
        case 3:++changed.identity.player;break;case 4:++changed.identity.machine;break;case 5:++changed.identity.peer;break;
        case 6:++changed.identity.steam;break;case 7:++changed.identity.authentication;break;case 8:++changed.identity.lifecycle;break;default:++changed.generation;break;}
        auto r=p.sample(changed,0,{jump,jump},move);if(field==8){check(!r && !p.flying(),"lifecycle change requires fresh G generation");++changed.generation;r=p.sample(changed,0,{jump,jump},move);}
        check(r && !r->climb && !p.flying(),"full identity/session/generation change resets toggle and active state");}
    for(auto native:{std::uint8_t(0),std::uint8_t(1),std::uint8_t(4),std::uint8_t(5),std::uint8_t(6),std::uint8_t(7),std::uint8_t(8),std::uint8_t(9),std::uint8_t(10)}){
        MovementPolicy p;p.sample(a,0,{},move);p.state(2);check(p.state(native)==native && !p.flying(),"native grounded/attachment/water/gliding priorities preserved");}
    {MovementPolicy p;p.sample(a,0,{},move);check(p.state(3)==3 && !p.flying(),"native Flying is not adopted without this G run");}
    {MovementPolicy p;auto invalid=move;invalid.x=std::numeric_limits<float>::quiet_NaN();check(!p.sample(a,0,{},invalid),"unknown/nonfinite movement unavailable");}
    {MovementPolicy p;auto invalid=move;invalid.z=std::numeric_limits<float>::max();check(!p.sample(a,0,{sprint,0},invalid),"scaled vector overflow fails closed");}
    std::cout<<checks<<" G flight movement checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
