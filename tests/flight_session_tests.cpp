#include "flight_session.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace xhl::flight;
namespace {
int checks = 0;
void check(bool value, const char* name) {
    ++checks; if (!value) throw std::runtime_error(name);
}
constexpr std::uint64_t alice = 0x0110000100000001, bob = 0x0110000100000002;
Allowlist config() { return parse_allowlist("schema=1\nsteam_id=" + std::to_string(alice) + "\nsteam_id=" + std::to_string(bob)); }
std::array<Identity,16> owners() {
    std::array<Identity,16> a{};
    a[0] = {1,2,3,64,128,64,alice,1};
    a[1] = {1,2,3,65,129,65,bob,2};
    return a;
}
bool actor(Sessions& s,std::uintptr_t world,std::uint32_t owner,std::uint64_t state,std::uint64_t now) {
    const auto sample=s.begin_actor_observation(world,owner,now);
    return sample && s.observe_actor(*sample,state,now);
}
void observe(Sessions& s,const std::array<Identity,16>& ids,std::uint64_t now) {
    s.observe(ids,now);
    for(std::size_t i=0;i<ids.size();++i)if(ids[i].valid(i))actor(s,ids[i].world,static_cast<std::uint32_t>(i+1),1,now);
}
Message handshake(Sessions& s, ClientSession& c, std::uint64_t id, std::uint64_t nonce, std::uint64_t now) {
    c.reset(nonce); auto result = s.receive(id,c.hello(),now,nonce+100);
    check(result.has_value(),"challenge"); check(c.receive(*result,now),"client challenge");
    return *c.request(true,now);
}
void observation_binding() {
    Sessions s;auto ids=owners();s.configure(config(),100);s.observe(ids,100);
    // Both initial samples deliberately share a millisecond; timestamps alone
    // cannot distinguish their order and the first sample does not bump life.
    const auto old_initial=*s.begin_actor_observation(3,1,100);
    const auto new_initial=*s.begin_actor_observation(3,1,100);
    s.observe_actor(new_initial,1ULL<<7,100);
    check(!s.observe_actor(old_initial,1,100) && s.actor_alive_for(*s.identity_for(alice,100),100)==false,"reversed initial samples cannot resurrect a dead actor even at equal timestamps");
    check(!s.observe_actor(new_initial,1,100),"a consumed sample ticket cannot publish another state");
    const auto late_live=*s.begin_actor_observation(3,1,101);
    const auto dead_sample=*s.begin_actor_observation(3,1,102);
    s.observe_actor(dead_sample,1ULL<<12,102);
    check(!s.observe_actor(late_live,1,103) && !s.alive_for(3,1,103),"a superseded live sample cannot overwrite later spawning evidence");
    const auto prior_owner=*s.begin_actor_observation(3,1,104);
    s.remove_owner(3,1);s.observe(ids,104);
    check(!s.observe_actor(prior_owner,1,104) && !s.actor_alive_for(*s.identity_for(alice,104),104),"reused owner handles reject a sample captured before removal");
    actor(s,3,1,1,105);
    actor(s,3,2,1,105);
    ClientSession client;const auto request=handshake(s,client,alice,700,105);s.receive(alice,request,105,0);
    check(s.can_fly(3,1,105),"failure fixture starts with live authorized flight");
    const auto before=*s.identity_for(alice,105),other=*s.identity_for(bob,105);
    const auto failed=*s.begin_actor_observation(3,1,106);
    check(s.observe_actor(failed,std::nullopt,106) && !s.can_fly(3,1,106) && !s.actor_alive_for(*s.identity_for(alice,106),106),"recognized null Actor or failed read immediately invalidates cached live execution evidence");
    check(s.identity_for(alice,106)->authentication==before.authentication && s.identity_for(alice,106)->lifecycle!=before.lifecycle && s.identity_for(bob,106)==other && s.alive_for(3,2,106),"read failure invalidates only that actor lifecycle and retains its connection and another actor's live evidence");
    actor(s,3,1,1,107);
    check(s.can_fly(3,1,107),"fresh recovery preserves only the existing unexpired flight preference");
    const auto old_failure=*s.begin_actor_observation(3,1,108);
    const auto newer_live=*s.begin_actor_observation(3,1,109);s.observe_actor(newer_live,1,109);
    check(!s.observe_actor(old_failure,std::nullopt,110) && s.can_fly(3,1,110),"superseded failure cannot invalidate newer live evidence");
    const auto delayed=*s.begin_actor_observation(3,1,111);
    s.observe(ids,1111);s.configure(config(),1111);
    check(s.observe_actor(delayed,1,1111) && !s.alive_for(3,1,1111) && !s.actor_alive_for(*s.identity_for(alice,1111),1111),"delayed publication cannot refresh stale sample time");
    const auto current=*s.begin_actor_observation(3,1,1112);
    check(!s.observe_actor(current,1,1111) && !s.alive_for(3,1,1111),"future-dated evidence cannot authorize execution");
}
void observation_deadline() {
    for(const auto state:std::array<std::optional<std::uint64_t>,3>{std::nullopt,1ULL<<7,1ULL<<12}) {
        Sessions s;const auto ids=owners();s.configure(config(),10000);s.observe(ids,10000);
        actor(s,3,1,1,10900);
        ClientSession client;const auto request=handshake(s,client,alice,900,10900);s.receive(alice,request,10900,0);
        check(s.can_fly(3,1,10900),"deadline fixture starts with live authorized flight");
        const auto sample=*s.begin_actor_observation(3,1,10999);
        check(s.observe_actor(sample,state,11001),"owner expiry at publication still invalidates the known actor lifecycle");
        s.observe(ids,11002);
        const auto current=*s.identity_for(alice,11002);
        check(current.authentication==sample.identity.authentication && current.lifecycle!=sample.identity.lifecycle,"deadline invalidation retains the connection and changes lifecycle");
        check(!s.alive_for(3,1,11002) && !s.can_fly(3,1,11002) && !s.actor_alive_for(current,11002),"fresh owner snapshot cannot revive evidence discarded at its deadline");
        auto consumed=sample;consumed.identity=current;
        // Isolate ticket consumption from the separate lifecycle rejection.
        check(!s.observe_actor(consumed,1,11002) && !s.alive_for(3,1,11002),"deadline path consumes its matching ticket even with current identity supplied");
        actor(s,3,1,1,11003);
        check(s.can_fly(3,1,11003),"only a successful recovery observation restores the retained unexpired preference");
    }
}
void respawn_preference() {
    Sessions s; auto ids=owners(); s.configure(config(),100); s.observe(ids,100);
    ClientSession client; auto request=handshake(s,client,alice,91,100);
    check(!s.actor_alive_for(*s.identity_for(alice,100),100),"unknown native actor is not classified as respawning");
    auto response=s.receive(alice,request,100,0);
    check(response && !response->enabled && !s.can_fly(3,1,100),"unknown actor cannot execute remembered flight");
    observe(s,ids,101);
    response=s.receive(alice,*client.request(true,101),101,0);
    check(response && response->enabled && client.receive(*response,101),"live approved actor executes requested flight");
    const auto before=*s.identity_for(alice,101), other=*s.identity_for(bob,101);
    check(actor(s,3,1,1ULL<<7,102),"native Dead flag starts a new actor lifecycle");
    check(!s.can_fly(3,1,102) && !s.flying_for(alice,102),"dead actor suspends actual flight");
    const auto dead=*s.identity_for(alice,102);
    check(s.actor_alive_for(dead,102)==false && !s.actor_alive_for(before,102),"only fresh matching identity reports known dead state");
    check(dead.authentication==before.authentication && dead.lifecycle!=before.lifecycle,"respawn retains connection but invalidates old actor work");
    check(s.identity_for(bob,102)==other,"another player's actor lifecycle is unchanged");
    response=s.receive(alice,*client.request(true,103),103,0);
    check(response && !response->enabled && client.receive(*response,103) && !client.can_fly(103),"death renewal is acknowledged without allowing flight");
    actor(s,3,1,1ULL<<12,104);
    check(!s.can_fly(3,1,104),"Spawning flag also suspends flight");
    check(actor(s,3,1,1,105),"live replacement actor gets a fresh lifecycle");
    check(!s.alive_for(before,105) && !s.alive_for(dead,105) &&
        s.alive_for(*s.identity_for(alice,105),105),"live replacement cannot validate a captured old actor identity");
    check(s.can_fly(3,1,105) && !client.can_fly(105),"server retains the enabled preference but client waits for a live acknowledgement");
    response=s.receive(alice,*client.request(true,106),106,0);
    check(response && response->enabled && client.receive(*response,106) && client.can_fly(106),"same authenticated connection resumes enabled flight after respawn");
    s.receive(alice,*client.request(false,107),107,0);
    actor(s,3,1,1ULL<<7,108);actor(s,3,1,1,109);
    check(!s.can_fly(3,1,109),"flight that was off stays off through respawn");
    s.receive(alice,*client.request(true,110),110,0);
    s.observe(ids,1110);
    check(!s.actor_alive_for(*s.identity_for(alice,1110),1110),"stale actor evidence is not a respawn keepalive");
    check(!s.can_fly(3,1,1110),"fresh connection alone cannot hide stale actor evidence");
    actor(s,3,1,1,1110);
    check(s.can_fly(3,1,1110),"fresh live evidence restores only the unexpired connection preference");
    s.configure({},1111);s.configure(config(),1112);actor(s,3,1,1,1112);
    check(!s.can_fly(3,1,1112) && !s.receive(alice,*client.request(true,1112),1112,0),"revoked approval erases the remembered preference and token");
    request=handshake(s,client,alice,92,1113);s.receive(alice,request,1113,0);
    s.remove_peer(1,64);observe(s,ids,1114);
    check(!s.can_fly(3,1,1114) && !s.receive(alice,request,1114,0),"reconnect with reused handles never inherits previous flight");
    check(!actor(s,9,1,1,1114) && !actor(s,3,17,1,1114),"actor observations cannot cross world or owner bounds");
}
}
int main() {
    try {
        observation_binding();
        observation_deadline();
        respawn_preference();
        check(config().valid && config().contains(alice),"config accepts individual Steam64");
        check(parse_allowlist("schema=1\n").valid,"empty allowlist valid denial");
        for (const auto& bad : {std::string{}, std::string("steam_id=1"), std::string("schema=2"),
             std::string("schema=1\nsteam_id=32817772"), std::string("schema=1\nunknown=true"),
             std::string("schema=1\nschema=1"), std::string("schema=1\nsteam_id=18446744073709551616"),
             std::string("schema=1\nsteam_id="+std::to_string(alice)+"junk"),
             std::string("schema=1\nsteam_id="+std::to_string(alice)+"\nsteam_id="+std::to_string(alice)),
             std::string(8193,' '), std::string("schema=1\0",9)})
            check(!parse_allowlist(bad).valid,"invalid config denies");
        auto a=owners(); Sessions server; server.configure(config(),100); observe(server,a,100);
        auto mapped=a[0];mapped.lifecycle=server.identity_for(alice,100)->lifecycle;
        check(mapped.lifecycle && server.identity_for(alice,100)==mapped,"shared authenticated sender mapping with lifecycle");
        check(server.identity_for(3,1,100)==mapped,"shared authenticated owner mapping");
        auto ordinary=server;observe(ordinary,a,101);
        check(ordinary.identity_for(alice,101)==mapped,"ordinary observation preserves lifecycle");
        check(!server.identity_for(9,1,100) && !server.identity_for(3,0,100),"shared mapping rejects other world or invalid owner");
        check(!server.identity_for(alice,1100) && !server.identity_for(3,1,99),"shared mapping requires fresh monotonic evidence");
        server.configure({},100);
        check(server.identity_for(alice,100).has_value() && !server.is_approved(alice,100),"authentication does not inherit feature privileges");
        server.configure(config(),100);
        ClientSession ac,bc; auto ar=handshake(server,ac,alice,10,100);
        check(!ac.can_fly(100) && !server.can_fly(3,1,100),"hello grants no flight");
        auto ack=server.receive(alice,ar,100,0); check(ack && ack->enabled,"authorized set");
        check(ac.receive(*ack,100) && ac.can_fly(100),"client waits for reply");
        check(server.can_fly(3,1,100) && !server.can_fly(3,2,100),"per owner isolation");
        check(!server.can_fly(9,1,100) && !server.can_fly(3,0,100) && !server.can_fly(3,17,100),"world and slot validation");
        check(!server.receive(bob,ar,100,0),"cross player request denied");
        check(!server.receive(alice+2,ar,100,0),"unmapped sender denied");
        auto br=handshake(server,bc,bob,20,100);
        auto ba=server.receive(bob,br,100,0); check(ba && bc.receive(*ba,100),"simultaneous second player");
        server.remove_peer(1,0);
        check(server.can_fly(3,1,100) && server.can_fly(3,2,100),"missing peer from failed authentication does not revoke either player");
        auto isolated=server;isolated.remove_peer(1,64);
        check(!isolated.can_fly(3,1,100) && isolated.can_fly(3,2,100),"known failed authentication revokes only that player");
        isolated.clear_backend(1);check(!isolated.can_fly(3,2,100),"explicit backend reset revokes remaining players");
        auto off=ac.request(false,200); check(off && !ac.can_fly(200),"local off immediate");
        auto oa=server.receive(alice,*off,200,0); check(oa && !oa->enabled,"server off");
        check(!server.can_fly(3,1,200) && server.can_fly(3,2,200),"different settings isolated");
        check(!server.receive(alice,ar,200,0),"old request cannot reenable");
        auto conflict=*off; conflict.enabled=true;
        check(!server.receive(alice,conflict,200,0),"sequence conflicting duplicate rejected");
        check(!ac.receive(*ack,200),"old on reply after off rejected");
        auto removed=config();removed.count=1;server.configure(removed,300);
        check(!server.can_fly(3,2,300) && !server.receive(bob,br,300,0),"allowlist revocation immediate");
        server.configure(config(),300);
        check(!server.receive(bob,br,300,0),"reapproval requires new handshake");
        server.configure({},400);check(!server.can_fly(3,1,400),"missing config denies");
        server.configure(config(),400);observe(server,a,400);
        ar=handshake(server,ac,alice,30,400);ack=server.receive(alice,ar,400,0);ac.receive(*ack,400);
        server.configure(config(),3400);observe(server,a,3400);
        auto duplicate=server.receive(alice,ar,3400,0);
        check(duplicate && !duplicate->enabled && !server.can_fly(3,1,3400),"duplicate cannot renew expired lease");
        check(!ac.receive(*ack,3400) && !ac.can_fly(3400),"delayed ack cannot extend lease");
        ar=handshake(server,ac,alice,40,3400);ack=server.receive(alice,ar,3400,0);ac.receive(*ack,3400);
        check(!server.can_fly(3,1,4400),"stale ownership denies");
        observe(server,a,5400);check(!server.can_fly(3,1,5400),"stale config denies");
        server.configure(config(),5400);observe(server,a,5400);
        ar=handshake(server,ac,alice,50,5400);server.receive(alice,ar,5400,0);
        a[0].player=128;observe(server,a,5401);
        check(!server.can_fly(3,1,5401) && !server.receive(alice,ar,5401,0),"reconnect invalidates token");
        ar=handshake(server,ac,alice,60,5401);server.receive(alice,ar,5401,0);
        a[0].authentication++;observe(server,a,5402);
        check(!server.can_fly(3,1,5402),"reauthentication clears activation");
        ar=handshake(server,ac,alice,70,5402);server.receive(alice,ar,5402,0);
        server.remove_peer(1,64);check(!server.can_fly(3,1,5402),"disconnect denies immediately");
        check(!server.identity_for(alice,5402) && !server.identity_for(3,1,5402),"shared identity removed immediately on disconnect");
        observe(server,a,5403);ar=handshake(server,ac,alice,80,5403);server.receive(alice,ar,5403,0);
        const auto before_reset=server.identity_for(alice,5403);
        const auto other_before=server.identity_for(bob,5403);
        server.remove_owner(3,1);check(!server.can_fly(3,1,5403),"character reset clears privilege");
        observe(server,a,5403);
        const auto after_reset=server.identity_for(alice,5403);
        check(before_reset && after_reset && before_reset->lifecycle!=after_reset->lifecycle,"immediate same-handle respawn invalidates queued identity");
        check(other_before==server.identity_for(bob,5403),"another player's lifecycle survives reset");
        check(!server.receive(alice,ar,5403,0) && !server.can_fly(3,1,5403),"old activation cannot survive immediate respawn");
        observe(server,a,5404);a[1].steam=alice;observe(server,a,5404);
        check(!server.receive(alice,ac.hello(),5404,99),"ambiguous identity denied");
        check(!server.identity_for(alice,5404),"shared identity rejects ambiguous peers");
        check(!server.can_fly(3,1,5404) && !server.can_fly(3,2,5404),"ambiguous mapping revokes existing flight");
        a=owners();a[0].authentication=0;observe(server,a,5405);
        check(!server.receive(alice,ac.hello(),5405,99),"no actual authentication denied");
        a=owners();a[0].player=65;observe(server,a,5405);
        check(!server.receive(alice,ac.hello(),5405,99),"slot generation mismatch denied");
        server.clear();check(!server.can_fly(3,2,5405),"teardown clears all");
        auto bytes=encode(ar);auto decoded=decode(bytes);check(decoded && encode(*decoded)==bytes,"wire roundtrip");
        for(std::size_t n=0;n<wire_size;++n)check(!decode(std::span(bytes).first(n)),"truncated wire rejected");
        for(std::size_t n : {std::size_t(0),std::size_t(8),std::size_t(10),std::size_t(12),std::size_t(13),std::size_t(14),std::size_t(44)}) {
            auto invalid=bytes;invalid[n]=255;check(!decode(invalid),"invalid wire header rejected");
        }
        ac.reset(123);check(!ac.can_fly(6000) && !ac.receive(*ack,6000),"client reconnect clears stale approval");
        std::cout<<checks<<" flight session checks passed (synthetic transport and identities).\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
