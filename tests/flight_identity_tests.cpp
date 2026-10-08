#include "flight_identity.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace xhl::flight;
namespace {
int checks=0;
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
std::vector<std::uint8_t> memory(0x400000);
constexpr std::uintptr_t state=0x100,channels=0x300000,session=0x301000,wrapper=0x320000,backend=0x321000,base=0x10000000,internal=session+0x4b68;
template<class T> void put(std::uintptr_t at,T value){std::memcpy(memory.data()+at,&value,sizeof(value));}
bool read(std::uintptr_t at,void* out,std::size_t size) noexcept {
    if(at>memory.size() || size>memory.size()-at)return false;
    std::memcpy(out,memory.data()+at,size);return true;
}
void setup(){
    std::fill(memory.begin(),memory.end(),std::uint8_t{0});
    put(state+0x1b0,std::uintptr_t(123));put(state+0x1c0,std::uint32_t(64));put(state+0x1c4,std::uint32_t(129));
    put(state+0x38,channels);put(channels,session);put(internal+0x2a60,std::uint32_t(64));put(internal+0x2ab4,std::uint32_t(129));
    put(internal+0x4770+0xb50,std::uint32_t(129));put(internal+0x4776+0xb50,std::uint16_t(64));
    put(session+0x10,wrapper);put(wrapper+0x18,backend);put(backend,base+0x1311048);
    const auto ctx=backend+0xd88;
    put(ctx+0x70,std::uint16_t(64));put(ctx+0x72,std::uint64_t(0x0110000100000001));put(ctx+0x108,std::uint8_t(2));
    put(ctx+0x1510,std::uint8_t(1));put(ctx+0x1512,std::uint8_t(1));
}
}
int main(){try{
    AuthProofs auth;Peer before{1,64,0x0110000100000001,1,true,true};auto after=before;after.state=2;
    check(!auth.serial(after),"native state two alone grants nothing");
    auto ticket=auth.begin(before);check(auth.finish(ticket,after,true) && auth.serial(after),"actual success transition");
    auto foreign=after;foreign.backend=2;check(!auth.serial(foreign),"backend mismatch");
    foreign=after;foreign.handle=128;check(!auth.serial(foreign),"peer generation mismatch");
    foreign=after;foreign.steam++;check(!auth.serial(foreign),"steam mismatch");
    foreign=after;foreign.auth_enabled=false;check(!auth.serial(foreign),"auth disabled denies");
    foreign=after;foreign.hosting=false;check(!auth.serial(foreign),"not a server denies");
    foreign=after;foreign.state=1;check(!auth.serial(foreign),"pending native authentication denies");
    auth.remove(1,64);check(!auth.serial(after),"removal invalidates proof");
    ticket=auth.begin(before);auth.remove(1,64);check(!auth.finish(ticket,after,true),"removal during callback cannot create proof");
    ticket=auth.begin(before);auth.clear_backend(1);check(!auth.finish(ticket,after,true),"reset during callback invalidates proof");
    ticket=auth.begin(after);check(!auth.finish(ticket,after,true),"state two before callback cannot grant proof");
    ticket=auth.begin(before);check(!auth.finish(ticket,after,false) && !auth.serial(after),"failed callback denies");
    ticket=auth.begin(before);foreign=after;foreign.handle=128;check(!auth.finish(ticket,foreign,true),"changed peer during callback denies");
    ticket=auth.begin(before);foreign=before;foreign.backend=2;auth.begin(foreign);check(!auth.finish(ticket,after,true),"backend replacement invalidates pending proof");
    AuthProofs isolated;auto second_before=before;second_before.handle=65;second_before.steam++;
    auto second_after=second_before;second_after.state=2;
    check(isolated.finish(isolated.begin(before),after,true) && isolated.finish(isolated.begin(second_before),second_after,true),"two independent peer proofs established");
    const auto first_serial=isolated.serial(after),second_serial=isolated.serial(second_after);
    isolated.remove(1,0);isolated.finish({},Peer{1,0,0,0,false,false},false);
    check(isolated.serial(after)==first_serial && isolated.serial(second_after)==second_serial,"unmatched failed callback cannot revoke other players");
    isolated.finish({},after,false);
    check(!isolated.serial(after) && isolated.serial(second_after)==second_serial,"known failed peer revokes only itself");
    isolated.clear_backend(1);check(!isolated.serial(second_after),"explicit backend reset revokes all proofs");
    setup();OwnerChain chain;Peer peer;
    check(read_owner_chain(read,state,base,0,chain),"native ownership chain resolves");
    check(chain.identity.player==64 && chain.identity.machine==129 && chain.identity.peer==64 && chain.identity.world==123,"full handles preserved");
    check(read_peer(read,backend,64,peer) && peer.auth_enabled && peer.hosting && peer.state==2,"native peer layout");
    check(!read_peer(read,backend,128,peer),"native peer generation validated");
    check(!read_owner_chain(read,state,base,16,chain) && !read_owner_chain(read,0,base,0,chain),"bad state/slot denied");
    for(auto address:{state+0x1b0,state+0x38,channels,session+0x10,wrapper+0x18,backend}){
        setup();put(address,std::uintptr_t(0));check(!read_owner_chain(read,state,base,0,chain),"broken pointer chain denies");
    }
    for(auto address:{state+0x1c0,state+0x1c4,internal+0x2a60,internal+0x2ab4,internal+0x4770+0xb50}){
        setup();put(address,std::uint32_t(256));check(!read_owner_chain(read,state,base,0,chain),"mismatched generations denied");
    }
    setup();put(internal+0x4776+0xb50,std::uint16_t(80));check(!read_owner_chain(read,state,base,0,chain),"out of range peer denied");
    setup();put(wrapper+0x18,UINTPTR_MAX);check(!read_owner_chain(read,state,base,0,chain),"invalid backend pointer denied");
    setup();put(backend+0xd88+0x1510,std::uint8_t(2));check(!read_peer(read,backend,64,peer),"invalid native boolean denied");
    setup();
    put(internal+0x2a60,std::uint32_t(0));
    put(session+0x2a60,std::uint32_t(64));put(session+0x2ab4,std::uint32_t(129));
    OwnerStage stage;
    check(!read_owner_chain(read,state,base,0,chain,&stage) && stage==OwnerStage::session_player,
        "lookalike record in published snapshots is never a live player record");
    setup();check(read_owner_chain(read,state,base,0,chain),"query test owner resolves");
    constexpr std::uintptr_t query=0x330000,context=0x331000,other_context=0x332000,execution_offset=0xcc4218;
    std::uintptr_t world=999;
    put(query,context);put(context,execution_offset+chain.identity.world);
    check(read_query_world(read,query,world) && world==chain.identity.world && world!=context,
        "temporary system context resolves to owning world");
    put(query,other_context);put(other_context,execution_offset+chain.identity.world);
    check(read_query_world(read,query,world) && world==chain.identity.world,
        "simulation thread context may change without changing world");
    Sessions sessions;std::array<Identity,16> ids{};ids[0]=chain.identity;
    ids[0].steam=0x0110000100000001;ids[0].authentication=1;
    Allowlist list;list.valid=true;list.ids[0]=ids[0].steam;list.count=1;
    sessions.configure(list,100);sessions.observe(ids,100);
    auto response=sessions.receive(ids[0].steam,{Kind::hello,false,Status::ok,10,0,0,client_revision},100,20);
    check(response.has_value(),"query test client authenticated");
    sessions.receive(ids[0].steam,{Kind::set,true,Status::ok,10,20,1,client_revision},100,0);
    check(sessions.can_fly(world,1,100),"resolved world reaches authorized flight");
    put(other_context,execution_offset+chain.identity.world+0x1000);
    check(read_query_world(read,query,world) && !sessions.can_fly(world,1,100),
        "same owner number in another world remains denied");
    for(auto bad:{std::uintptr_t(0),execution_offset-1,execution_offset}) {
        put(other_context,bad);world=999;
        check(!read_query_world(read,query,world) && world==0,"missing world and offset underflow fail closed");
    }
    put(query,std::uintptr_t(0));world=999;
    check(!read_query_world(read,query,world) && world==0,"missing context fails closed");
    put(query,UINTPTR_MAX);world=999;
    check(!read_query_world(read,query,world) && world==0,"unreadable context fails closed");
    check(!read_query_world(read,0,world) && !read_query_world(nullptr,query,world),"missing query or reader fails closed");
    std::cout<<checks<<" identity checks passed (synthetic native memory; no live authentication claim).\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
