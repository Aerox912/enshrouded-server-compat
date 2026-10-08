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
Message handshake(Sessions& s, ClientSession& c, std::uint64_t id, std::uint64_t nonce, std::uint64_t now) {
    c.reset(nonce); auto result = s.receive(id,c.hello(),now,nonce+100);
    check(result.has_value(),"challenge"); check(c.receive(*result,now),"client challenge");
    return *c.request(true,now);
}
}
int main() {
    try {
        check(config().valid && config().contains(alice),"config accepts individual Steam64");
        check(parse_allowlist("schema=1\n").valid,"empty allowlist valid denial");
        for (const auto& bad : {std::string{}, std::string("steam_id=1"), std::string("schema=2"),
             std::string("schema=1\nsteam_id=32817772"), std::string("schema=1\nunknown=true"),
             std::string("schema=1\nschema=1"), std::string("schema=1\nsteam_id=18446744073709551616"),
             std::string("schema=1\nsteam_id="+std::to_string(alice)+"junk"),
             std::string("schema=1\nsteam_id="+std::to_string(alice)+"\nsteam_id="+std::to_string(alice)),
             std::string(8193,' '), std::string("schema=1\0",9)})
            check(!parse_allowlist(bad).valid,"invalid config denies");
        auto a=owners(); Sessions server; server.configure(config(),100); server.observe(a,100);
        auto mapped=a[0];mapped.lifecycle=server.identity_for(alice,100)->lifecycle;
        check(mapped.lifecycle && server.identity_for(alice,100)==mapped,"shared authenticated sender mapping with lifecycle");
        check(server.identity_for(3,1,100)==mapped,"shared authenticated owner mapping");
        server.observe(a,101);
        check(server.identity_for(alice,101)==mapped,"ordinary observation preserves lifecycle");
        server.observe(a,100);
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
        server.configure(config(),400);server.observe(a,400);
        ar=handshake(server,ac,alice,30,400);ack=server.receive(alice,ar,400,0);ac.receive(*ack,400);
        server.configure(config(),3400);server.observe(a,3400);
        auto duplicate=server.receive(alice,ar,3400,0);
        check(duplicate && !duplicate->enabled && !server.can_fly(3,1,3400),"duplicate cannot renew expired lease");
        check(!ac.receive(*ack,3400) && !ac.can_fly(3400),"delayed ack cannot extend lease");
        ar=handshake(server,ac,alice,40,3400);ack=server.receive(alice,ar,3400,0);ac.receive(*ack,3400);
        check(!server.can_fly(3,1,4400),"stale ownership denies");
        server.observe(a,5400);check(!server.can_fly(3,1,5400),"stale config denies");
        server.configure(config(),5400);server.observe(a,5400);
        ar=handshake(server,ac,alice,50,5400);server.receive(alice,ar,5400,0);
        a[0].player=128;server.observe(a,5401);
        check(!server.can_fly(3,1,5401) && !server.receive(alice,ar,5401,0),"reconnect invalidates token");
        ar=handshake(server,ac,alice,60,5401);server.receive(alice,ar,5401,0);
        a[0].authentication++;server.observe(a,5402);
        check(!server.can_fly(3,1,5402),"reauthentication clears activation");
        ar=handshake(server,ac,alice,70,5402);server.receive(alice,ar,5402,0);
        server.remove_peer(1,64);check(!server.can_fly(3,1,5402),"disconnect denies immediately");
        check(!server.identity_for(alice,5402) && !server.identity_for(3,1,5402),"shared identity removed immediately on disconnect");
        server.observe(a,5403);ar=handshake(server,ac,alice,80,5403);server.receive(alice,ar,5403,0);
        const auto before_reset=server.identity_for(alice,5403);
        const auto other_before=server.identity_for(bob,5403);
        server.remove_owner(3,1);check(!server.can_fly(3,1,5403),"character reset clears privilege");
        server.observe(a,5403);
        const auto after_reset=server.identity_for(alice,5403);
        check(before_reset && after_reset && before_reset->lifecycle!=after_reset->lifecycle,"immediate same-handle respawn invalidates queued identity");
        check(other_before==server.identity_for(bob,5403),"another player's lifecycle survives reset");
        check(!server.receive(alice,ar,5403,0) && !server.can_fly(3,1,5403),"old activation cannot survive immediate respawn");
        server.observe(a,5404);a[1].steam=alice;server.observe(a,5404);
        check(!server.receive(alice,ac.hello(),5404,99),"ambiguous identity denied");
        check(!server.identity_for(alice,5404),"shared identity rejects ambiguous peers");
        check(!server.can_fly(3,1,5404) && !server.can_fly(3,2,5404),"ambiguous mapping revokes existing flight");
        a=owners();a[0].authentication=0;server.observe(a,5405);
        check(!server.receive(alice,ac.hello(),5405,99),"no actual authentication denied");
        a=owners();a[0].player=65;server.observe(a,5405);
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
