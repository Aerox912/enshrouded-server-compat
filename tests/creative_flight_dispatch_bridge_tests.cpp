#include "creative_flight_dispatch_bridge.hpp"
#include <Windows.h>
#include <intrin.h>
#include <array>
#include <iostream>
#include <stdexcept>
using namespace xhl::creative_flight;
extern "C" void creative_g_dispatch_fixture(std::uintptr_t*,void*) noexcept(false);
namespace {
int checks=0,route_calls=0,mode=0;
void check(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
struct Snapshot{
 std::array<std::uint64_t,16> gpr{};
 std::array<std::array<unsigned char,16>,16> xmm{};
 std::uint32_t mxcsr=0,padding=0;std::uint64_t flags=0;
};
static_assert(sizeof(Snapshot)==0x190);
std::array<std::uintptr_t,0x138/8> expected{};
DWORD seh_run(std::uintptr_t* row,void* snapshots){
 __try{creative_g_dispatch_fixture(row,snapshots);return 0;}
 __except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
}
}
extern "C" void creative_g_fixture_prepare(std::uintptr_t*,void*) {}
extern "C" void creative_g_fixture_complete(std::uintptr_t*) {}
extern "C" void creative_g_dispatch_route(std::uintptr_t* row,void* second) noexcept(false){
 ++route_calls;
 check(std::memcmp(row,expected.data(),0x138)==0,"actual full stack row survives bridge");
 check(*static_cast<std::uintptr_t*>(second)==0xabc,"native RBP second object preserved");
 if(mode==1)throw std::runtime_error("intentional bridge C++ exception");
 if(mode==2)RaiseException(0xe0470001,0,0,nullptr);
 // Deliberately alter volatile FP control/status. Bridge must restore original.
 _mm_setcsr(0x3f80);
}
int main(){try{
 constexpr std::uintptr_t base=0x140000000,site=base+0x187595;
 auto plan=make_dispatch_call_plan(base,true,site,site+0x1003, reinterpret_cast<std::uintptr_t>(&creative_g_dispatch_bridge),dispatch_displaced);
 check(plan.has_value(),"reachable aligned near slot produces plan");
 check(plan->after[0]==0xff && plan->after[1]==0x15 && plan->after[6]==0x90 && plan->after[7]==0x90,"exact CALL plus two NOPs");
 std::int32_t relative=0;std::memcpy(&relative,plan->after.data()+2,4);
 check(site+6+relative==plan->slot,"RIP-relative target is slot, not bridge");
 std::uintptr_t encoded=0;std::memcpy(&encoded,plan->slot_value.data(),8);
 check(encoded==plan->bridge && plan->before==dispatch_displaced,"slot contains full 64-bit bridge and exact restore bytes");
 auto refuse=[&](auto b,bool verified,auto s,auto slot,auto bridge,auto bytes){check(!make_dispatch_call_plan(b,verified,s,slot,bridge,bytes),"invalid installation contract refused");};
 refuse(base,false,site,site+0x1003,1,dispatch_displaced);
 refuse(base,true,site+1,site+0x1003,1,dispatch_displaced);
 refuse(base,true,site,site+0x1002,1,dispatch_displaced);
 refuse(base,true,site,0,1,dispatch_displaced);
 refuse(base,true,site,site+0x1003,0,dispatch_displaced);
 refuse(base,true,site,(site+6)+static_cast<std::uintptr_t>(INT32_MAX)+10,1,dispatch_displaced);
 refuse(base,true,site,(site+6)-static_cast<std::uintptr_t>(INT32_MAX)-10,1,dispatch_displaced);
 refuse(base,true,site,(site+3)&~std::uintptr_t(7),1,dispatch_displaced);
 refuse(base,true,site,site+0x1003,site,dispatch_displaced);
 refuse(base,true,site,site+0x1003,site+0x1003,dispatch_displaced);
 auto changed=dispatch_displaced;changed[0]^=1;refuse(base,true,site,site+0x1003,1,changed);
 refuse(base,true,site,site+0x1003,1,std::span<const std::uint8_t>(dispatch_displaced.data(),7));
 refuse(UINTPTR_MAX-0x187595,true,UINTPTR_MAX,8,1,dispatch_displaced);
 // Highest/lowest aligned representable slots; next byte's aligned slot fails.
 const auto next=site+6;
 const auto high=(next+INT32_MAX)&~std::uintptr_t(7);
 const auto low=(next-static_cast<std::uintptr_t>(INT32_MAX)-1+7)&~std::uintptr_t(7);
 check(make_dispatch_call_plan(base,true,site,high,1,dispatch_displaced).has_value(),"positive rel32 boundary accepted");
 check(make_dispatch_call_plan(base,true,site,low,1,dispatch_displaced).has_value(),"negative rel32 boundary accepted");
 refuse(base,true,site,high+8,1,dispatch_displaced);refuse(base,true,site,low-8,1,dispatch_displaced);
 std::array<unsigned char,0x100> dynamic{};
 expected.fill(0x12345678);expected[0x98/8]=reinterpret_cast<std::uintptr_t>(dynamic.data());
 for(unsigned state=0;state<256;++state){
  dynamic[0x3d]=static_cast<unsigned char>(state);std::array<Snapshot,2> snapshots{};
  creative_g_dispatch_fixture(expected.data(),snapshots.data());
  const auto& before=snapshots[0];const auto& after=snapshots[1];
  check(after.gpr[0]==expected[0x98/8] && after.gpr[1]==state,"displaced MOV outputs exactly match all uint8 states");
  for(unsigned reg=2;reg<16;++reg)check(before.gpr[reg]==after.gpr[reg],"all other GPRs/RSP/RBP preserved");
  check(before.xmm==after.xmm,"all sixteen XMM registers preserved");
  check(before.mxcsr==after.mxcsr,"MXCSR restored after C++ route");
  check(before.flags==after.flags,"original flags restored before unchanged native CMP");
 }

 // Exercise the OS unwinder using the actual emitted runtime-function record.
 DWORD64 image_base=0;const auto pc=reinterpret_cast<DWORD64>(&creative_g_dispatch_bridge);
 auto* function=RtlLookupFunctionEntry(pc,&image_base,nullptr);
 check(function!=nullptr,"assembled stub has discoverable Win64 runtime-function entry");
 const auto* header=reinterpret_cast<const unsigned char*>(image_base+function->UnwindData);
 alignas(16) std::array<unsigned char,0x200> stack{};
 auto write=[&](unsigned offset,DWORD64 value){std::memcpy(stack.data()+offset,&value,8);};
 const DWORD64 return_pc=0x123456789,old_rbp=0xabcdef012;
 write(0x1b8,return_pc);write(0x1b0,old_rbp);
 const std::array<unsigned,7> offsets{0x38,0x40,0x48,0x70,0x78,0x80,0x88};
 for(unsigned i=0;i<offsets.size();++i)write(offsets[i],0x100+i);
 for(unsigned r=6;r<16;++r)for(unsigned i=0;i<16;++i)stack[0x100+(r-6)*16+i]=static_cast<unsigned char>(r*16+i);
 CONTEXT context{};context.ContextFlags=CONTEXT_FULL;
 context.Rip=image_base+function->BeginAddress+header[1]+1;
 context.Rsp=reinterpret_cast<DWORD64>(stack.data());context.Rbp=context.Rsp;
 void* handler=nullptr;DWORD64 establisher=0;
 RtlVirtualUnwind(UNW_FLAG_NHANDLER,image_base,context.Rip,function,&context,&handler,&establisher,nullptr);
 check(context.Rip==return_pc && context.Rsp==reinterpret_cast<DWORD64>(stack.data())+0x1c0 && context.Rbp==old_rbp,"OS unwinder reaches real CALL return PC, native stack and original RBP");
 check(context.Rbx==0x100 && context.Rsi==0x101 && context.Rdi==0x102 && context.R12==0x103 && context.R13==0x104 && context.R14==0x105 && context.R15==0x106,"OS unwinder restores all saved nonvolatile GPRs");
 for(unsigned r=6;r<16;++r)check(std::memcmp((&context.Xmm0)+r,stack.data()+0x100+(r-6)*16,16)==0,"OS unwinder restores nonvolatile XMM registers");
 mode=1;bool threw=false;std::array<Snapshot,2> snapshots{};
 try{creative_g_dispatch_fixture(expected.data(),snapshots.data());}catch(const std::runtime_error&){threw=true;}
 check(threw,"real C++ exception unwinds across MASM CALL bridge and fixture FRAME");
 mode=2;check(seh_run(expected.data(),snapshots.data())==0xe0470001,"native SEH unwinds across MASM CALL bridge");
 check(route_calls==258,"route exactly once per normal and exceptional CALL");
 std::cout<<checks<<" G dispatch bridge checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
