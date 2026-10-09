#include "creative_flight_call_transaction.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace xhl::creative_flight::call_install;
using namespace xhl::creative_flight::call_install::detail;
namespace {
int checks=0;
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
struct Fake {
    Plan plan{};Recovery recovery{};
    std::array<Thread,8> threads{};
    std::array<std::uint32_t,8> ids{};
    std::array<std::uint8_t,8> memory=displaced;
    std::array<unsigned,8> increments{},resumed{},closed{},resume_calls{};
    unsigned frozen=0,enums=0,opens=0,suspends=0,rips=0,reads=0,protects=0,writes=0,flushes=0;
    unsigned enum_fail=0,open_fail=0,suspend_fail=0,rip_fail=0,read_fail=0,protect_fail=0,write_fail=0,flush_fail=0;
    unsigned partial=0,actual_partial=0,thread_count=2;
    bool churn=false,persistent_churn=false,duplicate=false,zero_id=false,proof_fail=false,foreign=false,resume_fail=false,resume_once=false;
    bool protected_rx=true;
    std::uintptr_t ip=0;
    Fake(){check(make_plan(0x140187595,0x140190000,0x180001000,plan),"fixture plan");ip=plan.site;increments[1]=2;}
    static Fake& f(void* p){return *static_cast<Fake*>(p);}
    static bool enumerate(void* p,std::span<std::uint32_t> out,std::size_t& n){auto& x=f(p);++x.enums;
        if(x.enums==x.enum_fail)return false;
        n=x.thread_count;
        if(x.churn&&(x.persistent_churn||x.enums==2))++n;
        if(n>out.size())return true;
        for(std::size_t i=0;i<n;++i)out[i]=static_cast<std::uint32_t>(i+1);
        if(x.duplicate&&n>=2)out[1]=out[0];if(x.zero_id&&n)out[0]=0;
        return true;
    }
    static bool open(void* p,std::uint32_t id,std::uintptr_t& h){auto& x=f(p);check(!x.frozen,"no OpenThread while frozen");
        if(++x.opens==x.open_fail)return false;h=id;return true;
    }
    static void close(void* p,std::uintptr_t h){auto& x=f(p);
        // Failed resumes retain their handles, but closing other discharged
        // handles is outside the attempted suspension interval.
        check(!x.frozen,"no handle close until ALL resumes succeed");++x.closed[h];
    }
    static bool suspend(void* p,std::uintptr_t h){auto& x=f(p);if(++x.suspends==x.suspend_fail)return false;
        ++x.increments[h];++x.frozen;return true;
    }
    static bool resume(void* p,std::uintptr_t h){auto& x=f(p);++x.resume_calls[h];
        check(x.memory==x.plan.before||x.memory==x.plan.after,"EVERY resume attempt requires complete site bytes");
        check(x.protected_rx,"EVERY resume attempt requires restored page protection");
        check(!x.recovery.modified||x.recovery.cache_proven,"EVERY resume attempt requires instruction cache proof after writes");
        if((x.resume_fail&&h==2)||(x.resume_once&&h==2&&x.resume_calls[h]==1))return false;
        check(x.increments[h]>0&&x.frozen>0,"resume only own increments");--x.increments[h];--x.frozen;++x.resumed[h];return true;
    }
    static bool rip(void* p,std::uintptr_t,std::uintptr_t& out){auto& x=f(p);if(++x.rips==x.rip_fail)return false;out=x.ip;return true;}
    static bool proof(void* p){return !f(p).proof_fail;}
    static bool read(void* p,std::uintptr_t,std::span<std::uint8_t> out){auto& x=f(p);if(++x.reads==x.read_fail)return false;
        std::memcpy(out.data(),x.memory.data(),out.size());return true;
    }
    static bool protect(void* p,std::uintptr_t,std::size_t,std::uint32_t flags,std::uint32_t& old){auto& x=f(p);
        old=x.protected_rx?0x20U:0x40U;if(++x.protects==x.protect_fail)return false;x.protected_rx=flags==0x20;return true;
    }
    static bool write(void* p,std::uintptr_t,std::span<const std::uint8_t> bytes,std::size_t& written){auto& x=f(p);check(!x.protected_rx,"write requires writable protection");
        if(++x.writes==x.write_fail){written=x.partial;std::memcpy(x.memory.data(),bytes.data(),x.actual_partial);if(x.foreign)x.memory[7]=0x42;return false;}
        written=bytes.size();std::memcpy(x.memory.data(),bytes.data(),written);return true;
    }
    static bool flush(void* p,std::uintptr_t,std::size_t){auto& x=f(p);return ++x.flushes!=x.flush_fail;}
    Result run(){Ops o{this,enumerate,open,close,suspend,resume,rip,proof,read,protect,write,flush};return detail_run(o);}
    Result detail_run(const Ops& o){Ops bound=o;bound.recovery=&recovery;return xhl::creative_flight::call_install::detail::run(plan,bound,threads,ids);}
    Result recover(){Ops o{this,enumerate,open,close,suspend,resume,rip,proof,read,protect,write,flush,&recovery};return xhl::creative_flight::call_install::detail::recover(o,threads);}
    void balanced(){check(!frozen,"all own suspends discharged");check(increments[1]==2&&increments[2]==0,"preexisting suspend count untouched");
        for(const auto& t:threads)check(!t.suspended&&t.handle==0,"discharged handles closed");}
};
}
int main(){try{
    {Fake f;auto r=f.run();check(r.status==Status::published&&r.published&&r.backing_retained,"publication succeeds");check(f.memory==f.plan.after&&f.protected_rx,"CALL bytes verified then RX");f.balanced();}
    for(unsigned offset=1;offset<8;++offset){Fake f;f.ip=f.plan.site+offset;auto r=f.run();check(r.status==Status::interior_ip&&!r.backing_retained&&!f.writes,"every interior RIP rejects before write");f.balanced();}
    for(auto offset:{std::uintptr_t(0),std::uintptr_t(8),std::uintptr_t(26)}){Fake f;f.ip=f.plan.site+offset;check(f.run().published,"boundary RIP is safe");f.balanced();}
    {Fake f;f.churn=true;auto r=f.run();check(r.published&&r.attempts==2,"new thread causes full thaw and retry");f.balanced();}
    {Fake f;f.churn=true;f.persistent_churn=true; // Change one ID on each frozen scan instead of count.
        auto o=Ops{&f,[](void* p,std::span<std::uint32_t> ids,std::size_t& n){auto& x=Fake::f(p);n=2;ids[0]=1;ids[1]=x.frozen?3U:2U;return true;},Fake::open,Fake::close,Fake::suspend,Fake::resume,Fake::rip,Fake::proof,Fake::read,Fake::protect,Fake::write,Fake::flush};
        auto r=f.detail_run(o);check(r.status==Status::unstable_threads&&r.attempts==4&&!f.writes,"bounded identity churn never publishes");f.balanced();}
    for(unsigned at=1;at<=3;++at){Fake f;f.enum_fail=at;check(f.run().status==Status::thread_failure,"every enumeration failure checked");f.balanced();}
    for(unsigned at=1;at<=2;++at){Fake f;f.open_fail=at;check(f.run().status==Status::thread_failure,"each open failure checked");f.balanced();}
    for(unsigned at=1;at<=2;++at){Fake f;f.suspend_fail=at;check(f.run().status==Status::thread_failure,"each suspension failure checked");f.balanced();}
    for(unsigned at=1;at<=2;++at){Fake f;f.rip_fail=at;check(f.run().status==Status::thread_failure,"each context failure checked");f.balanced();}
    {Fake f;f.duplicate=true;check(f.run().status==Status::thread_failure&&!f.opens,"duplicate initial IDs rejected");f.balanced();}
    {Fake f;f.zero_id=true;check(f.run().status==Status::thread_failure&&!f.opens,"zero initial ID rejected");f.balanced();}
    {Fake f;f.thread_count=9;check(f.run().status==Status::thread_failure&&!f.opens,"capacity overflow rejects");f.balanced();}
    {Fake f;f.proof_fail=true;check(f.run().status==Status::proof_failure&&!f.writes,"frozen proof failure rejects");f.balanced();}
    {Fake f;f.memory[0]^=1;auto r=f.run();check(r.status==Status::proof_failure&&!f.writes&&r.recovery_required&&f.frozen==2,"foreign initial mixed site rejected without unsafe resume");f.memory=displaced;check(!f.recover().recovery_required,"whole original proof permits recovery");f.balanced();}
    {Fake f;f.read_fail=1;check(f.run().status==Status::proof_failure&&!f.writes,"initial read failure rejects");f.balanced();}
    {Fake f;f.protect_fail=1;auto r=f.run();check(r.status==Status::protect_failure&&!r.backing_retained&&!f.writes,"initial protection failure rejects");f.balanced();}
    for(unsigned prefix=0;prefix<=8;++prefix){Fake f;f.write_fail=1;f.partial=prefix;f.actual_partial=prefix;auto r=f.run();
        check(r.status==Status::write_failure&&r.original_restored&&r.backing_retained&&!r.published,"reported partial prefix is safely rolled back");
        check(f.memory==displaced&&f.protected_rx,"rollback restores bytes and RX");f.balanced();}
    {Fake f;f.write_fail=1;f.partial=4;f.actual_partial=4;f.foreign=true;auto r=f.run();check(r.status==Status::rollback_failure&&r.backing_retained&&!r.original_restored,"foreign rollback refused");check(f.writes==1&&f.memory[7]==0x42&&f.protected_rx&&r.recovery_required&&f.frozen==2&&f.resumed[1]+f.resumed[2]==0,"foreign mixed bytes retain frozen ownership");
        auto still=f.recover();check(still.recovery_required&&f.writes==1&&f.frozen==2,"recovery never overwrites unowned fragment");
        f.memory=displaced;check(!f.recover().recovery_required,"external owner restoring complete original permits proof-based recovery");f.balanced();}
    {Fake f;f.read_fail=2;auto r=f.run();check(r.status==Status::rollback_failure&&r.backing_retained&&f.writes==1&&f.protected_rx,"unknown ownership retains slot and restores protection");f.balanced();}
    {Fake f;f.flush_fail=1;auto r=f.run();check(r.status==Status::cache_failure&&r.original_restored,"publication flush failure rolls back");f.balanced();}
    {Fake f;f.protect_fail=2;auto r=f.run();check(r.status==Status::protect_failure&&r.original_restored&&f.protected_rx,"restore failure reestablishes write then rolls back");f.balanced();}
    for(unsigned stage=0;stage<4;++stage){Fake f;f.write_fail=stage==0?2:1;f.actual_partial=f.partial=8;
        if(stage==0)f.flush_fail=1;
        if(stage==1)f.read_fail=3;
        if(stage==2)f.flush_fail=1; // failed write short-circuits initial flush; rollback flush is first
        if(stage==3)f.protect_fail=3;
        auto r=f.run();check(r.backing_retained&&r.site_complete&&!r.recovery_required,"complete rollback-stage result proved safe before resume");f.balanced();}
    {Fake f;f.resume_once=true;auto r=f.run();check(r.published&&r.status==Status::published&&f.resume_calls[2]==2&&f.resumed[2]==1,"transient resume retry never decrements twice");f.balanced();}
    {Fake f;f.resume_fail=true;auto r=f.run();check(r.status==Status::resume_failure&&r.published&&r.pending_suspends==1,"persistent resume failure exposes hard failure");
        check(f.resume_calls[2]==3&&f.resumed[1]==1&&f.resumed[2]==0&&!f.closed[1]&&!f.closed[2]&&f.threads[1].suspended&&f.threads[1].handle==2,"failed resume handle and exact ownership retained");}
    // VERIFY49's exact combination: 4-of-8 failed write, failed make-writable
    // rollback, successful RX restore. Neither thread may ever execute the mix.
    {Fake f;f.write_fail=1;f.partial=4;f.actual_partial=4;f.protect_fail=2;
        auto r=f.run();check(r.status==Status::rollback_failure&&!r.original_restored&&r.recovery_required&&!r.site_complete&&r.pending_suspends==2,"VERIFY49 mixed site retains explicit recovery ownership");
        check(f.frozen==2&&f.resumed[1]+f.resumed[2]==0&&f.memory!=displaced&&f.memory!=f.plan.after&&f.protected_rx,"VERIFY49 never resumes mixed instruction despite RX restored");
        check(f.threads[0].suspended&&f.threads[1].suspended&&f.threads[0].handle&&f.threads[1].handle&&!f.closed[1]&&!f.closed[2],"all suspended handles retained");
        Ops invalid_ops{};invalid_ops.recovery=&f.recovery;
        auto invalid_recovery=xhl::creative_flight::call_install::detail::recover(invalid_ops,f.threads);
        check(invalid_recovery.status==Status::invalid_request&&invalid_recovery.recovery_required&&invalid_recovery.pending_suspends==2&&f.frozen==2,"invalid recovery callback set cannot erase retained ownership");
        const auto opens=f.opens,suspends=f.suspends,enums=f.enums;
        check(f.run().status==Status::already_attempted&&f.recovery.active,"retained ownership blocks a new transaction");
        f.read_fail=f.reads+1;auto read_failed=f.recover();check(read_failed.recovery_required&&f.frozen==2&&f.resumed[1]+f.resumed[2]==0,"failed recovery byte proof keeps both frozen");
        f.protect_fail=f.protects+1;auto protect_failed=f.recover();check(protect_failed.recovery_required&&f.frozen==2&&f.resumed[1]+f.resumed[2]==0,"failed recovery protection keeps both frozen");
        f.write_fail=f.writes+1;f.partial=f.actual_partial=2;auto write_failed=f.recover();
        check(write_failed.recovery_required&&!write_failed.site_complete&&f.frozen==2&&f.resumed[1]+f.resumed[2]==0&&f.protected_rx,"partial recovery write retains ownership and independently restores RX");
        check(f.recovery.fragment_known&&f.memory==f.recovery.owned_fragment,"partial recovery records exact newly owned fragment");
        f.flush_fail=f.flushes+1;auto flush_failed=f.recover();
        check(flush_failed.recovery_required&&flush_failed.status==Status::cache_failure&&flush_failed.site_complete&&f.memory==displaced&&f.frozen==2&&f.resumed[1]+f.resumed[2]==0,"complete recovered bytes still cannot resume until cache proof succeeds");
        auto recovered=f.recover();check(recovered.status==Status::recovered&&!recovered.recovery_required&&recovered.site_complete&&recovered.original_restored&&f.memory==displaced&&f.protected_rx,"later bounded recovery restores/proves original before resume");
        check(f.opens==opens&&f.suspends==suspends&&f.enums==enums&&f.resumed[1]==1&&f.resumed[2]==1,"recovery opens/suspends/enumerates nothing and discharges once");f.balanced();}
    {Fake f;f.thread_count=0;f.write_fail=1;f.partial=f.actual_partial=4;f.protect_fail=2;
        auto r=f.run();check(r.recovery_required&&r.pending_suspends==0&&f.recovery.active,"unsafe zero-peer transaction still retains gate/backing ownership");
        check(f.recover().original_restored&&!f.recovery.active,"zero-peer recovery requires complete site proof too");f.balanced();}
    {Fake f;f.resume_fail=true;auto r=f.run();check(r.recovery_required&&r.site_complete,"complete site resume failure is recoverable");
        f.resume_fail=false;auto recovered=f.recover();check(!recovered.recovery_required&&recovered.published&&f.resumed[1]==1&&f.resumed[2]==1,"resume-only recovery never repeats discharged increment");f.balanced();}
    {Fake f;f.plan.after[0]=0xe9;check(f.run().status==Status::invalid_request&&!f.opens,"JMP substitute rejected");f.balanced();}
    {Plan p;check(!make_plan(0,8,1,p),"null site rejects");check(!make_plan(UINTPTR_MAX-7,8,1,p),"overflow rejects");
        check(!make_plan(0x100000000,0x100000003,1,p),"unaligned slot rejects");
        check(!make_plan(0x100000000,0x100000008,1,p),"dispatch overlap rejects");
        check(!make_plan(0x100000000,0x100100000,0x100000010,p),"bridge cannot overlap continuation");
        const std::uintptr_t next=0x140187595+6,high=(next+INT32_MAX)&~std::uintptr_t(7),low=(next-std::uintptr_t(INT32_MAX)-1+7)&~std::uintptr_t(7);
        check(make_plan(next-6,high,1,p)&&make_plan(next-6,low,1,p),"both aligned rel32 boundaries accepted");
        check(!make_plan(next-6,high+8,1,p)&&!make_plan(next-6,low-8,1,p),"outside rel32 boundaries refused");}
    std::cout<<checks<<" transaction checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"check "<<checks<<": "<<e.what()<<'\n';return 1;}}
