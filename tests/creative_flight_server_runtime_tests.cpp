#include "creative_flight_server_runtime.hpp"
#include <Windows.h>
#include <chrono>
extern "C" void creative_g_dispatch_fixture(std::uintptr_t*,void*) noexcept(false);
#include <future>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>
using namespace xhl::creative_flight;
namespace {
int checks=0;void check(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
struct Fixture;
Fixture* current=nullptr;
struct Query {std::uintptr_t definition=0;std::uint32_t cursor=0;ServerPhase phase=ServerPhase::control;};
struct Fixture {
 ServerGRuntime runtime;MovementApproval approved;
 static constexpr std::uintptr_t world=0x60000000,execution=world+0xcc4218;
 std::array<std::uintptr_t,0x800/8> definition{};
 std::array<unsigned char,0xe10> actor{},actor2{};
 std::array<unsigned char,0x528> player{};
 std::array<unsigned char,0x1b8> actor_input{};
 std::array<unsigned char,0x1c0> config{};
 std::array<unsigned char,0x138> dynamic{};
 std::array<unsigned char,0x14> gravity{};
 std::array<std::uintptr_t,0x138/8> row{};
 Query query;
 std::vector<std::uint32_t> owners{1};
 std::uint64_t clock=1000;Vector3 native_move{1,0,0.5f},target{};
 std::array<unsigned,4> callback_calls{};
 unsigned steps=0,state_calls=0,native_mover_calls=0,dive_calls=0,gravity_effect=0,reads=0,auths=0;
 std::uint8_t native_state=2,last_state=0;
 bool denied=false,bad_row=false,no_rows=false,throw_gravity=false,throw_dive=false,throw_state=false,throw_control=false;
 bool use_bridge=false,bridge_restored=false,seh_dive=false,seh_gravity=false;
 std::uintptr_t* frame_row=nullptr;void* frame_second=nullptr;
 bool read_throw=false,reentrant_stop=false,reentrant_result=true,failed_before_dive=false;
 bool observation_missing=false;std::uint64_t observed_life=40;
 unsigned fail_read=0,revoke_auth=0;std::function<void(std::uintptr_t)> mutation;
 std::function<void()> approval_mutation;
 std::promise<void>* entered=nullptr;std::shared_future<void> release;
 template<class T>static std::uintptr_t at(T& object){return reinterpret_cast<std::uintptr_t>(&object);}
 template<class T,class A>static void put(A& bytes,std::size_t offset,T value){std::memcpy(bytes.data()+offset,&value,sizeof(value));}
 Fixture(){current=this;definition[0]=execution;query.definition=at(definition);
  approved.identity={1,2,world,64,128,64,0x0110000100000001,5,6};approved.generation=7;approved.actor=at(actor);
  config.fill(0xa5);put(dynamic,0x3d,std::uint8_t(3));gravity[0x10]=1;
  put(player,0x320,1ull<<jump_action);put(actor_input,0x198,native_move);
 }
 bool read(std::uintptr_t address,void* out,std::size_t size){
  ++reads;if(read_throw)throw std::runtime_error("provider read");if(reads==fail_read){failed_before_dive=!dive_calls;return false;}
  if(address==execution && size==8){auto registry=world+0x928;std::memcpy(out,&registry,8);return true;}
  auto copy=[&](auto& bytes){auto start=at(bytes);if(address>=start && address-start<=sizeof(bytes) && size<=sizeof(bytes)-(address-start)){std::memcpy(out,reinterpret_cast<void*>(address),size);return true;}return false;};
  bool success=frame_row && address>=reinterpret_cast<std::uintptr_t>(frame_row) && address-reinterpret_cast<std::uintptr_t>(frame_row)<=0x138 && size<=0x138-(address-reinterpret_cast<std::uintptr_t>(frame_row));
  if(success)std::memcpy(out,reinterpret_cast<void*>(address),size);
  success=success||copy(definition)||copy(query)||copy(actor)||copy(actor2)||copy(player)||copy(actor_input)||copy(config)||copy(dynamic)||copy(gravity)||copy(row);
  if(success && mutation)mutation(address);
  return success;
 }
 std::optional<MovementApproval> approve(std::uintptr_t w,std::uint32_t owner,std::uintptr_t a){++auths;if(denied || (revoke_auth && auths>=revoke_auth) || w!=world || owner!=1 || a!=approved.actor)return {};if(approval_mutation)approval_mutation();return approved;}
 std::optional<ServerObservationBinding> observe_binding(std::uintptr_t w,std::uint32_t owner,std::uintptr_t a){
  if(denied || observation_missing || w!=world || owner!=1 || a!=approved.actor)return {};
  return ServerObservationBinding{approved.identity,observed_life,w,a,owner};
 }
 static void* entity(void* pointer,void* out){auto& q=*static_cast<Query*>(pointer);std::uint32_t value=q.cursor && q.cursor<=current->owners.size()?current->owners[q.cursor-1]:0;std::memcpy(out,&value,4);return out;}
 static bool iterator(void* pointer,void* output,std::uint32_t size){auto& f=*current;auto& q=*static_cast<Query*>(pointer);++f.steps;
  if(f.no_rows || q.cursor>=f.owners.size())return false;
  auto owner=f.owners[q.cursor++];auto* r=static_cast<std::uintptr_t*>(output);std::fill_n(r,size/8,std::uintptr_t(0));r[0]=at(q);
  auto a=owner==1?at(f.actor):at(f.actor2);
  switch(q.phase){case ServerPhase::control:r[0x48/8]=a;r[0x20/8]=at(f.player);r[0x58/8]=at(f.actor_input);break;
   case ServerPhase::state:r[0x10/8]=a;break;
   case ServerPhase::mover:r[0x68/8]=a;r[0x10/8]=at(f.config);r[0x98/8]=at(f.dynamic);r[0xb8/8]=0x9010;r[0xc0/8]=0x9020;break;
   case ServerPhase::gravity:r[1]=a;r[2]=at(f.gravity);r[3]=0x9030;r[4]=0x9040;r[5]=0x9050;break;}
  if(f.bad_row)r[0]=0;return true;
 }
 static std::uint8_t native(void* a,void* actor_value,void* c,void* d,void* e,void* ff,void* g,void* h){auto& f=*current;++f.state_calls;
  check(a==reinterpret_cast<void*>(1) && c==reinterpret_cast<void*>(3) && d==reinterpret_cast<void*>(4) && e==reinterpret_cast<void*>(5) && ff==reinterpret_cast<void*>(6) && g==reinterpret_cast<void*>(7) && h==reinterpret_cast<void*>(8),"eight original state arguments preserved");
  check(actor_value==reinterpret_cast<void*>(at(f.actor)) || actor_value==reinterpret_cast<void*>(at(f.actor2)),"original receives current Actor");
  if(f.throw_state)throw std::runtime_error("state original");return f.native_state;
 }
 static void native_mover(std::uintptr_t* r,void* second){auto& f=*current;++f.native_mover_calls;check(r==f.row.data() && second==reinterpret_cast<void*>(0xabc),"vanilla mover frame and second argument preserved");}
 static void dive(std::uintptr_t* r,void* second){auto& f=*current;++f.dive_calls;
  check(r==(f.use_bridge?f.frame_row:f.row.data()) && second==(f.use_bridge?f.frame_second:reinterpret_cast<void*>(0xabc)),"Dive receives whole actual row and second argument");
  check(r[0x10/8]!=at(f.config) && r[0x98/8]!=at(f.dynamic),"Dive uses only private scratch pointers");
  check(r[0xb8/8]==0x9010 && r[0xc0/8]==0x9020,"native component/counter tail retained");
  std::memcpy(&f.target,reinterpret_cast<void*>(r[0x98/8]),sizeof(f.target));float accel=0;std::memcpy(&accel,reinterpret_cast<void*>(r[0x10/8]+0x13c),4);check(accel==40,"private Dive acceleration");
  if(f.seh_dive)RaiseException(0xe0470002,0,0,nullptr);
  if(f.throw_dive)throw std::runtime_error("Dive original");
 }
 static void original(void* pointer){auto& f=*current;auto& q=*static_cast<Query*>(pointer);++f.callback_calls[static_cast<unsigned>(q.phase)];
  if(f.entered){f.entered->set_value();f.release.wait();return;}
  if(f.reentrant_stop){f.reentrant_result=f.runtime.stop();return;}
  if(f.use_bridge && q.phase==ServerPhase::mover){std::array<unsigned char,0x320> snapshots{};creative_g_dispatch_fixture(f.row.data(),snapshots.data());f.frame_row=nullptr;f.frame_second=nullptr;return;}
  const std::array<std::uint32_t,4> sizes{0xb0,0x70,0x138,0x30};
  while(f.runtime.query_step(pointer,f.row.data(),sizes[static_cast<unsigned>(q.phase)],iterator)){
   if(q.phase==ServerPhase::control){if(f.throw_control)throw std::runtime_error("control original");put(f.actor_input,0x198,f.native_move);}
   if(q.phase==ServerPhase::state){f.last_state=f.runtime.state_decision(reinterpret_cast<void*>(1),reinterpret_cast<void*>(f.row[2]),reinterpret_cast<void*>(3),reinterpret_cast<void*>(4),reinterpret_cast<void*>(5),reinterpret_cast<void*>(6),reinterpret_cast<void*>(7),reinterpret_cast<void*>(8),native);put(f.dynamic,0x3d,f.last_state);}
   if(q.phase==ServerPhase::mover){
    ServerGRuntime::dispatch_current(f.row.data(),reinterpret_cast<void*>(0xabc));
    // Pinned tables both map 3/5/6/7/8 (and >10) directly to continuation.
    // Actual table semantics are also checked against both PE images by the
    // separate verifier. This fixture never fabricates a state3 glider call.
    const auto state=f.dynamic[0x3d];
    if(state==0 || state==1 || state==2 || state==4 || state==9 || state==10)native_mover(f.row.data(),reinterpret_cast<void*>(0xabc));
   }
   if(q.phase==ServerPhase::gravity){check(f.row[3]==0x9030 && f.row[4]==0x9040 && f.row[5]==0x9050,"gravity remaining components unchanged");auto* value=reinterpret_cast<unsigned char*>(f.row[2]);if(value[0x10])++f.gravity_effect;if(f.seh_gravity)RaiseException(0xe0470003,0,0,nullptr);if(f.throw_gravity)throw std::runtime_error("gravity original");}
  }
 }
 bool start(bool movement=true,bool observe=false,bool ordered=true,bool pinned=true){return runtime.start({
  [&](auto a,auto o,auto n){return read(a,o,n);},
  [&](auto w,auto owner,auto a){return approve(w,owner,a);},
  [&](auto w,auto owner,auto a){return observe_binding(w,owner,a);},
  [&]{return clock;},entity,dive},{movement,observe,pinned,ordered});}
 void run(ServerPhase phase){query.phase=phase;query.cursor=0;runtime.callback(phase,&query,original);}
 void prepare(){run(ServerPhase::control);run(ServerPhase::state);}
};
DWORD seh_run(ServerPhase phase){
 __try{current->run(phase);return 0;}
 __except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
}
}
extern "C" void creative_g_fixture_prepare(std::uintptr_t* row,void* second){
 auto& f=*current;f.frame_row=row;f.frame_second=second;
 check(f.runtime.query_step(&f.query,row,0x138,Fixture::iterator),"actual assembly frame captured by native iterator route");
}
extern "C" void creative_g_fixture_complete(std::uintptr_t* row){
 auto& f=*current;f.bridge_restored=row[0x10/8]==Fixture::at(f.config) && row[0x98/8]==Fixture::at(f.dynamic);
}
int main(){try {
 {Fixture f;f.run(ServerPhase::control);check(f.callback_calls[0]==1 && f.steps==2 && !f.reads && !f.auths,"default-off callbacks exactly once without adapter reads");}
 {Fixture f;check(!f.start(false,false),"default-off start rejected");check(!f.start(true,false,false),"movement requires local phase acceptance");check(!f.start(false,true,true,false),"phase observation requires full pinned server");}
 {Fixture f;check(f.start(),"explicit validated opt-in starts uninstalled adapter");f.prepare();check(f.last_state==3 && f.callback_calls[0]==1 && f.callback_calls[1]==1 && f.state_calls==1,"completed control selects Falling to Flying once");f.run(ServerPhase::mover);check(f.dive_calls==1 && !f.native_mover_calls && f.target==Vector3{1.5f,7.5f,0.75f},"configured held Jump native camera-relative movement");check(f.row[2]==Fixture::at(f.config) && f.row[19]==Fixture::at(f.dynamic) && f.config[0x13c]==0xa5,"mover pointers restore shared config unchanged");f.run(ServerPhase::mover);check(f.dive_calls==1 && !f.native_mover_calls,"same command cannot replay Dive; Flying continues without native mover");}
 {Fixture f;f.start();f.denied=true;f.prepare();check(f.last_state==2,"unauthorized state remains native");f.run(ServerPhase::mover);check(!f.dive_calls && f.native_mover_calls==1,"unauthorized mover original once");f.run(ServerPhase::gravity);check(f.gravity_effect==1 && f.gravity[0x10]==1,"unauthorized gravity original unchanged");}
 {Fixture f;f.start();f.prepare();Fixture::put(f.dynamic,0x3d,std::uint8_t(4));f.run(ServerPhase::mover);check(!f.dive_calls && f.native_mover_calls==1,"state4 stays on native glider route");}
 {Fixture f;f.start();f.prepare();f.use_bridge=true;f.run(ServerPhase::mover);check(f.dive_calls==1 && f.bridge_restored && !f.frame_row,"real CALL bridge consumes scoped current row and restores pointers before native continuation");}
 {Fixture f;f.start();f.prepare();f.seh_dive=true;check(seh_run(ServerPhase::mover)==0xe0470002 && f.row[2]==Fixture::at(f.config) && f.row[19]==Fixture::at(f.dynamic) && f.runtime.stop(),"runtime /EHa restores private Dive pointers and drains through native SEH");}
 {Fixture f;f.start();f.prepare();f.seh_gravity=true;check(seh_run(ServerPhase::gravity)==0xe0470003 && f.row[2]==Fixture::at(f.gravity) && f.runtime.stop(),"runtime /EHa restores private Gravity pointer and drains through native SEH");}
 {Fixture f;f.start();f.prepare();f.run(ServerPhase::gravity);check(!f.gravity_effect && f.gravity[0x10]==1 && f.row[2]==Fixture::at(f.gravity),"private gravity gate restores and never mutates shared component");}
 {Fixture f;f.start();f.prepare();f.throw_gravity=true;bool threw=false;try{f.run(ServerPhase::gravity);}catch(...){threw=true;}check(threw && f.row[2]==Fixture::at(f.gravity) && f.callback_calls[3]==1,"gravity C++ exception restores row original once");}
 {Fixture f;f.start();f.prepare();f.throw_dive=true;bool threw=false;try{f.run(ServerPhase::mover);}catch(...){threw=true;}check(threw && f.dive_calls==1 && !f.native_mover_calls && f.row[2]==Fixture::at(f.config) && f.row[19]==Fixture::at(f.dynamic),"Dive unwind restores row without double native movement");}
 {Fixture f;f.start();f.prepare();f.denied=true;f.run(ServerPhase::mover);check(!f.dive_calls && !f.native_mover_calls,"revocation after selection preserves native Flying continuation");f.denied=false;f.prepare();check(f.last_state==2,"unchanged G generation cannot revive retired flight");++f.approved.generation;f.prepare();check(f.last_state==3,"new G generation requires new control sample");}
 for(auto bits:{1ull<<7,1ull<<12}){Fixture f;f.start();f.prepare();Fixture::put(f.actor,0xbf8,bits);f.run(ServerPhase::mover);check(!f.dive_calls && !f.native_mover_calls,"Dead/Spawning cannot execute retained movement");}
 for(auto bits:{1ull<<7,1ull<<12,1ull}){Fixture f;f.start();f.prepare();Fixture::put(f.actor,0x1b1,std::uint8_t(1));Fixture::put(f.actor,0xbd0,bits);f.run(ServerPhase::mover);check(!f.dive_calls,"predicted Dead/Spawning/Grounded blocks retained G command");}
 {Fixture f;f.start();f.prepare();Fixture::put(f.actor,0xbf8,1ull<<7);Fixture::put(f.actor,0x1b1,std::uint8_t(1));Fixture::put(f.actor,0xbd8,1ull<<7);f.run(ServerPhase::mover);check(!f.dive_calls,"raw Dead remains denied when prediction removes it");}
 {Fixture f;f.start();f.prepare();Fixture::put(f.actor,0x1b1,std::uint8_t(1));Fixture::put(f.actor,0xbd0,1ull<<7);f.run(ServerPhase::mover);Fixture::put(f.actor,0xbd0,std::uint64_t(0));Fixture::put(f.actor,0x1b1,std::uint8_t(0));f.prepare();check(f.last_state==2,"effective death retirement cannot resume on unchanged G heartbeat");}
 {Fixture f;f.start();f.prepare();Fixture::put(f.actor,0x1b1,std::uint8_t(1));f.mutation=[&](auto address){if(address==Fixture::at(f.actor)+0xbd0)Fixture::put(f.actor,0xbd0,1ull<<7);};f.run(ServerPhase::mover);check(!f.dive_calls,"changing prediction fields fail closed");}
 {Fixture f;f.start();f.prepare();++f.approved.identity.lifecycle;f.run(ServerPhase::mover);check(!f.dive_calls,"full lifecycle mismatch discards retained sample");}
 {Fixture f;f.start();f.prepare();++f.approved.identity.peer;f.run(ServerPhase::mover);check(!f.dive_calls,"peer mismatch cannot execute copied command");}
 for(unsigned failure=1;failure<=120;++failure){Fixture f;f.start();f.prepare();f.fail_read=f.reads+failure;f.run(ServerPhase::mover);if(f.failed_before_dive)check(!f.dive_calls && !f.native_mover_calls,"every pre-Dive memory failure preserves original and cannot execute copied movement");}
 {Fixture f;f.start();f.prepare();f.clock+=101;f.run(ServerPhase::mover);check(!f.dive_calls && !f.native_mover_calls,"expired input command cannot execute");}
 {Fixture f;f.start();f.prepare();f.clock-=1;f.run(ServerPhase::gravity);check(f.gravity_effect==1,"clock rollback suspends gravity cancellation");}
 {Fixture f;f.start();f.prepare();f.native_state=1;f.run(ServerPhase::control);f.run(ServerPhase::state);f.run(ServerPhase::gravity);check(f.last_state==1 && f.gravity_effect==1,"native landing clears local flight and restores gravity");}
 {Fixture f;f.start();f.prepare();Fixture::put(f.actor,0xbf8,std::uint64_t(1));f.run(ServerPhase::gravity);check(f.gravity_effect==1,"fresh grounded Actor cancels gravity override before state update");}
 {Fixture f;f.start();f.prepare();Fixture::put(f.actor,0xbf8,std::uint64_t(1));f.run(ServerPhase::mover);check(!f.dive_calls && !f.native_mover_calls,"fresh Grounded actor cannot execute stale Flying movement");Fixture::put(f.actor,0xbf8,std::uint64_t(0));f.prepare();check(f.last_state==3,"ordinary landing clears local run without fabricating new G generation");}
 for(bool predicted:{false,true}){Fixture f;f.start();f.prepare();const auto generation=f.approved.generation;
  if(predicted){Fixture::put(f.actor,0x1b1,std::uint8_t(1));Fixture::put(f.actor,0xbd0,1ull);}else Fixture::put(f.actor,0xbf8,1ull);
  f.run(ServerPhase::gravity);check(f.gravity_effect==1,"raw/effective landing restores gravity");
  f.native_state=1;f.prepare();f.run(ServerPhase::mover);check(f.last_state==1 && !f.dive_calls && f.native_mover_calls==1,"landed approved G actor walks through native state/mover");
  Fixture::put(f.actor,0xbf8,std::uint64_t(0));Fixture::put(f.actor,0xbd0,std::uint64_t(0));f.native_state=2;f.prepare();f.run(ServerPhase::mover);
  check(f.last_state==3 && f.dive_calls==1 && f.approved.generation==generation,"land walk jump resumes same armed G generation without toggle");}
 for(auto trigger:{std::size_t(0x3d),std::size_t(0)}){Fixture f;f.start();f.prepare();
  f.mutation=[&](auto address){if(address==(trigger?Fixture::at(f.dynamic)+trigger:Fixture::at(f.config))){Fixture::put(f.actor,0x1b1,std::uint8_t(1));Fixture::put(f.actor,0xbd0,1ull);}};
  f.run(ServerPhase::mover);check(!f.dive_calls && f.row[2]==Fixture::at(f.config) && f.row[19]==Fixture::at(f.dynamic),"late effective grounding cancels Dive and restores row");
  f.mutation={};Fixture::put(f.actor,0xbd0,std::uint64_t(0));f.prepare();f.run(ServerPhase::mover);check(f.dive_calls==1,"late landing preserves armed G binding for fresh jump");}
 {Fixture f;f.start();f.run(ServerPhase::control);const auto next=f.auths+2;
  f.approval_mutation=[&]{if(f.auths==next)Fixture::put(f.actor,0xbf8,1ull);};f.run(ServerPhase::state);
  check(f.last_state==2,"grounding during state authorization preserves native Falling decision");
  f.approval_mutation={};Fixture::put(f.actor,0xbf8,std::uint64_t(0));f.prepare();f.run(ServerPhase::mover);check(f.last_state==3 && f.dive_calls==1,"state-time grounding ends run without retiring G generation");}
 for(auto bits:{1ull<<7,1ull<<12}){Fixture f;f.start();f.prepare();
  f.mutation=[&](auto address){if(address==Fixture::at(f.config)){Fixture::put(f.actor,0x1b1,std::uint8_t(1));Fixture::put(f.actor,0xbd0,bits);}};
  f.run(ServerPhase::mover);check(!f.dive_calls && f.row[2]==Fixture::at(f.config),"late effective Dead/Spawning denies native Dive");
  f.mutation={};Fixture::put(f.actor,0xbd0,std::uint64_t(0));f.prepare();check(f.last_state==2,"late effective death/spawn retires unchanged G generation");}
 {Fixture f;f.start();f.prepare();f.no_rows=true;f.run(ServerPhase::control);f.no_rows=false;f.run(ServerPhase::state);check(f.last_state==2,"new empty control callback supersedes previous command");}
 {Fixture f;f.start();f.prepare();f.bad_row=true;f.run(ServerPhase::control);f.bad_row=false;f.run(ServerPhase::state);check(f.last_state==2,"recognized owner failed row retires execution state");}
 {Fixture f;f.start();f.run(ServerPhase::state);check(f.last_state==2,"state before completed control is untouched");f.run(ServerPhase::control);f.run(ServerPhase::state);check(f.last_state==3,"later valid callback order can establish first run");}
 {Fixture f;f.start();f.run(ServerPhase::control);++f.approved.generation;f.run(ServerPhase::state);check(f.last_state==2,"new activation cannot inherit old generation input");}
 {Fixture f;f.start();f.owners={1,2};f.prepare();check(f.state_calls==2 && f.last_state==2,"nonplayer-approved row stays native in mixed callback");f.run(ServerPhase::gravity);check(f.gravity_effect==1,"gravity override only current authorized owner");}
 {Fixture f;f.start();f.query.phase=ServerPhase::control;f.query.cursor=0;check(f.runtime.query_step(&f.query,f.row.data(),0xb0,Fixture::iterator) && !f.reads && !f.auths,"global iterator outside scoped callback is fast passthrough");}
 {Fixture f;f.start();f.prepare();f.runtime.stop();f.run(ServerPhase::mover);check(!f.native_mover_calls && !f.dive_calls,"stopped adapter preserves native Flying continuation");}
 {Fixture f;f.start();f.read_throw=true;f.prepare();check(f.last_state==2 && f.steps==4,"provider failure cannot suppress/double original iterator");}
 {Fixture f;f.start();f.throw_control=true;bool threw=false;try{f.run(ServerPhase::control);}catch(...){threw=true;}check(threw && f.callback_calls[0]==1,"control original exception forwarded exactly once");f.throw_control=false;f.run(ServerPhase::state);check(f.last_state==2,"incomplete control output cannot authorize");}
 {Fixture f;f.start();f.throw_state=true;f.run(ServerPhase::control);bool threw=false;try{f.run(ServerPhase::state);}catch(...){threw=true;}check(threw && f.state_calls==1 && f.callback_calls[1]==1,"native state exception does not call original twice");}
 {Fixture f;check(f.start(false,true,false),"read-only phase mode starts without movement acceptance");f.denied=true;f.clock=100000;
  f.run(ServerPhase::control);check(f.runtime.take_phase_events().empty(),"private observation stays dormant until local authorization");
  check(f.runtime.reset_phase_observer(),"local observer command authorizes one bounded phase window");
  f.run(ServerPhase::control);check(f.runtime.take_phase_events().empty(),"unauthenticated observation binding records no events");f.denied=false;f.run(ServerPhase::control);f.run(ServerPhase::state);f.run(ServerPhase::mover);f.run(ServerPhase::gravity);auto events=f.runtime.take_phase_events();check(events.size()==4 && events[0].elapsed_ms==0 && events[3].order==4,"first current binding starts bounded ordered phase events");check(!f.dive_calls && f.native_mover_calls==1 && f.gravity_effect==1,"phase mode performs no movement/gravity mutation");f.denied=true;f.run(ServerPhase::control);f.denied=false;f.clock+=100;f.run(ServerPhase::control);check(f.runtime.take_phase_events().empty(),"observer stops permanently on binding loss");}
 {Fixture f;f.start(false,true);f.runtime.reset_phase_observer();f.run(ServerPhase::control);f.runtime.take_phase_events();f.clock+=20000;f.run(ServerPhase::state);check(f.runtime.take_phase_events().empty(),"phase capture deadline bound");}
 {Fixture f;f.start(false,true);f.runtime.reset_phase_observer();for(unsigned n=0;n<210;++n){f.clock+=50;f.run(ServerPhase::control);}auto events=f.runtime.take_phase_events();check(events.size()<=200,"phase observation attempts/events bounded");}
 {Fixture f;check(f.start(false,true,false,true),"observe-only runtime starts with phase acceptance closed");f.denied=true;
  for(unsigned n=0;n<12;++n){if(n)f.clock+=100;for(auto phase:{ServerPhase::control,ServerPhase::state,ServerPhase::mover,ServerPhase::gravity})f.run(phase);}
  check(f.runtime.phase_evidence().event_count==0,"observer has no events without a current authenticated binding");f.denied=false;
  check(f.runtime.reset_phase_observer(),"local observation reset starts a fresh bounded window");
  for(unsigned n=0;n<12;++n){if(n)f.clock+=100;for(auto phase:{ServerPhase::control,ServerPhase::state,ServerPhase::mover,ServerPhase::gravity})f.run(phase);}
  const auto evidence=f.runtime.phase_evidence();check(evidence.complete&&evidence.phase_mask==0x0f&&evidence.event_count>=16&&evidence.elapsed_ms>=1000,"pointer-free observer evidence completes with all phases and repeated events");
  f.observation_missing=true;check(!f.runtime.accept_phase_order(evidence),"phase acceptance rechecks current owner/life/Actor binding");f.observation_missing=false;
  check(f.runtime.accept_phase_order(evidence)&&f.runtime.phase_evidence().accepted,"explicit local acceptance records only the exact current evidence");
  check(f.runtime.enable_movement()&&!f.runtime.reset_phase_observer(),"movement opens only after accepted current evidence and observer cannot be reset");
  check(!f.dive_calls,"observe-only sampling never invokes Dive movement");
  check(f.native_mover_calls==24,"observe-only sampling preserves every native mover route");}
 {Fixture f;f.start();f.reentrant_stop=true;f.run(ServerPhase::control);check(!f.reentrant_result && !f.runtime.active() && f.runtime.stop(),"reentrant stop disables and requires later external drain");}
 {Fixture f;f.start();std::promise<void> entered,release;f.entered=&entered;f.release=release.get_future().share();auto native=std::async(std::launch::async,[&]{f.run(ServerPhase::control);});entered.get_future().wait();auto stopped=std::async(std::launch::async,[&]{return f.runtime.stop();});check(stopped.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout,"stop retains provider/context while callback active");release.set_value();native.get();check(stopped.get(),"external stop drains current callback before destruction");}
 std::cout<<checks<<" server G runtime checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

