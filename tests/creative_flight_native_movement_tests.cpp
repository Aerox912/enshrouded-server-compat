#include "creative_flight_native_movement.hpp"
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>
using namespace xhl::creative_flight;
namespace {
int checks=0;void check(bool v,const char* label){++checks;if(!v)throw std::runtime_error(label);}
struct Fixture {
    MovementPolicy policy;MovementApproval approval;
    std::array<std::uintptr_t,0x138/8> row{};
    std::map<std::uintptr_t,std::vector<unsigned char>> memory;
    unsigned reads=0,auth=0,calls=0,fail_read=0,revoke_at=0,die_at_read=0;bool throwing=false;
    Fixture(){approval.identity={1,2,3,64,128,64,0x0110000100000001,5,6};approval.generation=7;approval.actor=0x1000;
        memory[0x1000].resize(0xc00);memory[0x2000].resize(0x528);memory[0x3000].resize(0x1b8);
        memory[0x4000].resize(0x1c0,0xa5);memory[0x5000].resize(0x138);set(0x5000+0x3d,std::uint8_t(3));
        set(0x2000+0x320,1ull<<jump_action);set(0x3000+0x198,Vector3{1,0,0.5f});
        row[0x68/8]=0x1000;row[0x10/8]=0x4000;row[0x98/8]=0x5000;row[0xb8/8]=0x9000;row[0xc0/8]=0x9008;
    }
    template<class T>void set(std::uintptr_t at,T value){for(auto& [base,bytes]:memory)if(at>=base && at-base<=bytes.size() && sizeof(T)<=bytes.size()-(at-base)){std::memcpy(bytes.data()+at-base,&value,sizeof(T));return;}throw std::runtime_error("fixture write");}
    bool read(std::uintptr_t at,void* out,std::size_t size){++reads;if(reads==die_at_read)set(0x1000+0xbf8,1ull<<7);if(reads==fail_read)return false;
        for(const auto& [base,bytes]:memory)if(at>=base && at-base<=bytes.size() && size<=bytes.size()-(at-base)){std::memcpy(out,bytes.data()+at-base,size);return true;}return false;}
    std::optional<MovementApproval> authorize(){++auth;if(revoke_at && auth>=revoke_at)return {};return approval;}
    std::optional<MovementCommand> sample(){return read_movement_input(policy,{approval,0x2000,0x3000},[&]{return authorize();},[&](auto a,auto o,auto n){return read(a,o,n);});}
    std::uint8_t select(std::uint8_t native){return select_g_state(policy,approval,native,[&]{return authorize();},[&](auto a,auto o,auto n){return read(a,o,n);});}
    bool dive(const MovementCommand& command){return invoke_g_dive(policy,approval,command,MoverRow(row),reinterpret_cast<void*>(0xabc),
        [&]{return authorize();},[&](auto a,auto o,auto n){return read(a,o,n);},[&](std::uintptr_t* native,void* second){++calls;
            check(native==row.data() && second==reinterpret_cast<void*>(0xabc),"whole native row and original second argument forwarded");
            check(native[0xb8/8]==0x9000 && native[0xc0/8]==0x9008,"native velocity and counter slots preserved");
            check(native[0x10/8]!=0x4000 && native[0x98/8]!=0x5000,"only private scratch substitutes shared config/target");
            float accel=0,decel=0;std::memcpy(&accel,reinterpret_cast<void*>(native[0x10/8]+0x13c),4);std::memcpy(&decel,reinterpret_cast<void*>(native[0x10/8]+0x140),4);
            check(accel==40 && decel==40,"original Creative dive acceleration constants");
            check(*reinterpret_cast<Vector3*>(native[0x98/8])==command.target,"copied target remains valid through native invocation");
            check(*reinterpret_cast<unsigned char*>(native[0x10/8])==0xa5,"other native config fields retained");
            if(throwing)throw std::runtime_error("native callback test exception");});}
};
}
int main(){try{
    {Fixture f;auto command=f.sample();check(command && command->climb,"proven raw action input read");check(f.reads==5 && f.auth==2,"bounded before/after input capture");
        f.select(2);check(f.dive(*command) && f.calls==1,"approved active G dispatches native Dive");
        check(f.row[2]==0x4000 && f.row[19]==0x5000,"query pointers restored after native call");
        check(f.memory[0x4000][0x13c]==0xa5 && f.memory[0x5000][0]==0,"shared components not written");
        check(!f.dive(*command) && f.calls==1,"same input command cannot replay native movement");}
    {Fixture f;check(f.select(2)==2 && !f.reads,"unsampled approval cannot select G flight");}
    {Fixture f;f.sample();f.set(0x1000+0xbf8,1ull<<7);check(f.select(2)==2 && !f.policy.flying(),"Dead actor cannot select flight");}
    {Fixture f;f.sample();++f.approval.generation;check(f.select(2)==2 && !f.policy.flying(),"new approval cannot reuse prior input binding");}
    {Fixture f;f.sample();f.revoke_at=f.auth+2;check(f.select(2)==2 && !f.policy.flying(),"revocation during state read preserves native result");}
    {Fixture f;f.sample();check(f.select(2)==3 && f.policy.flying(),"current G state helper selects Falling to Flying");check(f.select(0)==0 && !f.policy.flying(),"state helper preserves grounded landing");}
    {Fixture f;f.revoke_at=1;check(!f.sample() && !f.reads,"denied approval reads no native memory");}
    {Fixture f;f.revoke_at=2;check(!f.sample() && !f.policy.flying(),"revocation during input read invalidates copied command");}
    for(unsigned i=1;i<=5;++i){Fixture f;f.fail_read=i;check(!f.sample(),"each missing input field fails closed");}
    {Fixture f;f.set(0x1000+0xbf8,1ull<<7);check(!f.sample(),"Dead raw frame cannot approve movement");}
    {Fixture f;auto command=*f.sample();f.select(2);f.row[13]=0x1001;check(!f.dive(command) && !f.calls,"mover actor must match approved actor");}
    {Fixture f;auto command=*f.sample();check(!f.dive(command) && !f.calls,"native Flying without G-local active run is unchanged");}
    {Fixture f;auto command=*f.sample();f.select(2);f.set(0x5000+0x3d,std::uint8_t(2));check(!f.dive(command) && !f.calls,"native mover state mismatch fails closed");}
    {Fixture f;auto command=*f.sample();f.select(2);f.set(0x1000+0xbf8,1ull<<12);check(!f.dive(command) && !f.calls,"Spawning after input sample cannot execute Dive");}
    {Fixture f;auto old=*f.sample();f.select(2);f.sample();check(!f.dive(old) && !f.calls,"older input sample cannot execute after newer frame");}
    {Fixture f;auto command=*f.sample();f.select(2);f.die_at_read=f.reads+4;check(!f.dive(command) && !f.calls,"death during config copy rejected before native call");}
    {Fixture f;auto command=*f.sample();f.select(2);f.revoke_at=f.auth+2;check(!f.dive(command) && !f.calls,"revocation during config read prevents Dive");}
    {Fixture f;auto command=*f.sample();f.select(2);f.revoke_at=f.auth+3;check(f.dive(command) && !f.policy.flying(),"post-call revoke clears all local state");
        check(f.row[2]==0x4000 && f.row[19]==0x5000,"revocation still restores native row");}
    {Fixture f;auto command=*f.sample();f.select(2);f.throwing=true;bool threw=false;try{f.dive(command);}catch(const std::runtime_error&){threw=true;}
        check(threw && !f.policy.flying() && f.row[2]==0x4000 && f.row[19]==0x5000,"C++ unwind restores native row");}
    {Fixture f;MovementInputFrame frame{f.approval,UINTPTR_MAX-8,0x3000};check(!read_movement_input(f.policy,frame,[&]{return f.authorize();},[&](auto a,auto o,auto n){return f.read(a,o,n);}),"input pointer overflow unavailable");}
    std::cout<<checks<<" native G movement checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
