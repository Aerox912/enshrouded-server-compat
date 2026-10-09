#include "creative_flight_g_integration.hpp"
#include "creative/native_client_g_integration.h"
#include <Windows.h>
#include <iostream>
#include <stdexcept>
using namespace xhl::creative_flight;
namespace ci=xhl::creative_flight::call_install;
namespace cc=creative::native_client;
namespace {
int checks=0;void check(bool ok,const char* label){++checks;if(!ok)throw std::runtime_error(label);}
struct Fake {
    std::atomic<unsigned> installs{0},recoveries{0};std::atomic<DWORD> owner{0};
    std::atomic<bool> fail_recovery{true},wrong_thread{false};bool initial_hold=false;
    static ci::Result complete() noexcept {
        ci::Result r;r.status=ci::Status::published;r.published=true;r.backing_retained=true;r.site_complete=true;return r;
    }
    static ci::Result install(void* p) noexcept {
        auto& f=*static_cast<Fake*>(p);++f.installs;f.owner.store(GetCurrentThreadId());auto r=complete();
        if(f.initial_hold){r.status=ci::Status::rollback_failure;r.published=false;r.site_complete=false;r.recovery_required=true;r.pending_suspends=2;}return r;
    }
    static ci::Result recover(void* p) noexcept {
        auto& f=*static_cast<Fake*>(p);++f.recoveries;
        if(f.owner.load()!=GetCurrentThreadId())f.wrong_thread.store(true);
        auto r=complete();if(f.fail_recovery.load()){r.status=ci::Status::rollback_failure;r.published=false;r.site_complete=false;r.recovery_required=true;r.pending_suspends=2;}return r;
    }
};
// All service state and injected contexts have process lifetime. Worker code
// performs no allocation/logging/callback into the game, even in held fixtures.
ci::InstallService server,client,held,rejected;
Fake sf,cf,hf;
bool await_state(ci::InstallService& service,ci::ServiceState state){
    const auto end=GetTickCount64()+5000;while(GetTickCount64()<end){if(service.state()==state)return true;Sleep(1);}return false;
}
ci::EntryProof proof(ci::Image image){return {image,1,true,true,true};}
cc::ClientGContext context(){
    cc::ClientGContext c;c.lease.approved=true;c.lease.epoch=4;c.lease.capabilities=creative::capability_bit(creative::Capability::creative_flight);
    c.protocol_session={11,12};c.host_generation=2;c.observation_generation=1;c.now_ms=100;c.expires_at_ms=4100;
    c.life=cc::LocalLifeSnapshot{{1,2,3,4,5,6,0x80000401u},0,0};c.reported_g=false;return c;
}
void acknowledge(cc::ClientGActivation& activation,cc::ClientGContext& c,std::uint64_t sequence){
    creative::Request r;r.op=creative::Op::creative_flight;r.flags=1;r.session=c.protocol_session;r.sequence=sequence;
    cc::LocalLifeTicket ticket{c.host_generation,c.observation_generation,c.lease.epoch,c.life->identity,true};
    check(activation.begin(r,ticket,c),"current local life ticket precedes finalized G send");
    creative::Response reply;reply.session=r.session;reply.sequence=r.sequence;reply.status=creative::Status::ok;reply.creative_flight_known=true;reply.creative_flight=true;
    c.reported_g=true;check(activation.accept(reply,true,c),"authenticated matching acknowledgment supplies local activation");
}
}
int main(){try{
    check(!server.ready(ci::Image::server)&&!server.safe_to_report(),"capability and reporting default off");
    for(unsigned mask=0;mask<7;++mask){auto p=proof(ci::Image::server);p.full_pinned_file=(mask&1)!=0;p.callback_bytes=(mask&2)!=0;p.matching_frame_bridge=(mask&4)!=0;
        check(!rejected.start_test(p,Fake::install,Fake::recover,&sf),"missing any entry proof rejects before thread/install");}
    check(sf.installs==0,"invalid proof invokes no installer");
    check(server.start_test(proof(ci::Image::server),Fake::install,Fake::recover,&sf)&&await_state(server,ci::ServiceState::settled),"persistent server owner installs then settles");
    check(sf.installs==1&&sf.owner!=GetCurrentThreadId()&&server.safe_to_report(),"installation executes once on dedicated owner thread");
    check(!server.start(proof(ci::Image::server))&&!server.ready(ci::Image::server),"start is one-shot and publication alone advertises no capability");
    for(unsigned mask=0;mask<7;++mask)check(!server.accept_runtime({(mask&1)!=0,(mask&2)!=0,(mask&4)!=0})&&!server.ready(ci::Image::server),"runtime requires originals, current binding provider and phase acceptance together");
    check(server.accept_runtime({true,true,true})&&server.ready(ci::Image::server)&&!server.ready(ci::Image::client),"installed exact-image gate enables only accepted runtime");
    MovementApproval approval{{1,2,3,64,128,64,0x0110000100000001,5,6},7,8};unsigned authorizations=0;
    auto authorize=[&](std::uintptr_t,std::uint32_t,std::uintptr_t)->std::optional<MovementApproval>{++authorizations;return approval;};
    check(gated_server_g_approval(server,true,3,1,8,authorize)==approval,"server forwards exact existing G authority without altering generation");
    check(!gated_server_g_approval(server,false,3,1,8,authorize)&&authorizations==1,"inactive runtime invokes no authority provider");
    check(!gated_server_g_approval(server,true,4,1,8,authorize)&&!gated_server_g_approval(server,true,3,2,8,authorize)&&!gated_server_g_approval(server,true,3,1,9,authorize),"world, owner and actor mismatches reject");
    approval.generation=0;check(!gated_server_g_approval(server,true,3,1,8,authorize),"no fabricated server generation on absent authority");approval.generation=7;
    server.disable();check(!server.ready(ci::Image::server)&&!gated_server_g_approval(server,true,3,1,8,authorize)&&server.snapshot().published,"logical disable closes capability without unpatching");
    hf.initial_hold=true;check(held.start_test(proof(ci::Image::server),Fake::install,Fake::recover,&hf)&&await_state(held,ci::ServiceState::parked),"unsafe failure retains persistent owner after bounded recovery batch");
    check(hf.recoveries>=3&&held.snapshot().pending_suspends==2&&!held.safe_to_report()&&!held.accept_runtime({true,true,true})&&!held.ready(ci::Image::server),"failed batch cannot report, activate or discard retained suspensions");
    const auto attempts=hf.recoveries.load();Sleep(30);check(hf.recoveries==attempts,"parked owner waits in kernel instead of spinning");
    held.disable();check(held.snapshot().recovery_required&&held.request_recovery()&&await_state(held,ci::ServiceState::parked),"logical disable cannot cancel retained recovery");
    hf.fail_recovery=false;check(held.request_recovery()&&await_state(held,ci::ServiceState::settled),"later recovery request settles safely on same persistent owner");
    check(!hf.wrong_thread&&hf.installs==1&&!held.snapshot().recovery_required&&held.safe_to_report()&&!held.ready(ci::Image::server),"recovery never migrates thread or implicitly enables G");
    check(client.start_test(proof(ci::Image::client),Fake::install,Fake::recover,&cf)&&await_state(client,ci::ServiceState::settled)&&client.accept_runtime({true,true,true}),"client has independent exact-image readiness");
    cc::ClientGActivation activation;auto c=context();
    check(!cc::gated_client_g_approval(client,true,activation,c),"ready client and reported preference alone confer no G authority");
    acknowledge(activation,c,1);auto local=cc::gated_client_g_approval(client,true,activation,c);
    check(local&&local->activation==1&&local->identity==c.life->identity,"client forwards acknowledged local token and copied tags only");
    c.lease.epoch++;check(!cc::gated_client_g_approval(client,true,activation,c),"changed lease cannot retain local G binding");
    acknowledge(activation,c,2);check(cc::gated_client_g_approval(client,true,activation,c)->activation==2,"new acknowledgment advances only local token");
    client.disable();check(!cc::gated_client_g_approval(client,true,activation,c),"readiness loss revokes client activation");
    check(client.accept_runtime({true,true,true})&&!cc::gated_client_g_approval(client,true,activation,c),"restoring readiness cannot resurrect old acknowledgment");
    acknowledge(activation,c,3);check(!cc::gated_client_g_approval(client,false,activation,c),"runtime drain revokes acknowledged prediction before destruction");
    std::cout<<checks<<" integration seam checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"check "<<checks<<": "<<e.what()<<'\n';return 1;}}
