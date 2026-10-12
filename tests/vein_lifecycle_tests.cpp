// SPDX-License-Identifier: MIT
// Public CI fixture: production packet/cache/detour code, synthetic native
// entries, inert original callbacks. No game files, server, network or world.
#include "../src/vein_radius.cpp"
#include "../src/server_lifecycle.cpp"
#include <cstdio>
#include <cstdlib>
#include <semaphore>
#include <thread>

namespace {
unsigned checks=0, originals=0, observers=0;
DWORD clock_value=1000;
thread_local bool pause_clock=false;
std::binary_semaphore clock_entered{0}, clock_resume{0};
void check(bool ok,const char* label) {
    if(!ok){std::printf("FAIL: %s\n",label);std::exit(1);}++checks;
}
DWORD WINAPI clock_stub(){
    if(pause_clock){clock_entered.release();clock_resume.acquire();}
    return clock_value;
}
int unpack_stub(void*,void*){return 19;}
std::uintptr_t player_stub(void*,void*){++originals;return 71;}
std::uintptr_t world_stub(void*){++originals;return 72;}
void player_observer(void*,void*){++observers;}
void world_observer(void*){++observers;}
void send_raw(DWORD owner,DWORD encoded) {
    std::array<unsigned char,0x410> packet{};auto message=packet.data()+16;
    vein_radius::store(packet.data(),0,owner);
    vein_radius::store(message,0x378,BYTE(1));
    vein_radius::store(message,0x37c,DWORD(0x434c4858));
    vein_radius::store(message,0x380,encoded);
    check(vein_radius::receive(nullptr,message)==19,"ordinary unpack forwarded");
    check(vein_radius::load<BYTE>(message,0x378)==0 && vein_radius::load<std::uint64_t>(message,0x37c)==0,
        "request stripped before stock unpack");
}
void send(DWORD owner,DWORD sequence,unsigned radius=1) {
    send_raw(owner,DWORD(0xa6000000|((radius-1)<<22)|sequence));
}
}
int main(int argc,char**) {
    std::array<unsigned char,0x50000> plugin{};vein_radius::plugin=plugin.data();
    vein_radius::store(plugin.data(),0x4c830,clock_stub);
    vein_radius::store(plugin.data(),0x49538,unpack_stub);
    auto game=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x690000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    check(game!=nullptr,"allocate synthetic native image");
    const unsigned char player_bytes[]={0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x81,0xec,0x90,0,0,0,0x0f,0xb6,0x82};
    const unsigned char world_bytes[]={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57};
    std::memcpy(game+0x68a7e0,player_bytes,sizeof(player_bytes));
    std::memcpy(game+0x68a430,world_bytes,sizeof(world_bytes));
    check(MH_Initialize()==MH_OK,"initialize native hook fixture");
    game[0x68a430]=0xcc;
    check(!xhl::lifecycle::prepare(reinterpret_cast<HMODULE>(game)) && game[0x68a7e0]==0x48,
        "changed lifecycle signature rejects before touching either entry");
    game[0x68a430]=world_bytes[0];
    check(xhl::lifecycle::prepare(reinterpret_cast<HMODULE>(game)),"prepare real lifecycle detours");
    if(argc>1) {
        xhl::lifecycle::cancel();
        check(MH_ApplyQueued()==MH_OK && game[0x68a7e0]==0x48 && game[0x68a430]==0x48,
            "a later component cannot activate cancelled lifecycle hooks");
        check(!xhl::lifecycle::prepare(reinterpret_cast<HMODULE>(game)),"cancelled lifecycle installation cannot report ready");
        MH_Uninitialize();VirtualFree(game,0,MEM_RELEASE);return 0;
    }
    check(MH_ApplyQueued()==MH_OK,"install real lifecycle detours");
    check(xhl::lifecycle::prepare(reinterpret_cast<HMODULE>(game)),"shared runtime cannot install duplicate hooks");
    xhl::lifecycle::original_player_reset=player_stub;xhl::lifecycle::original_teardown=world_stub;
    xhl::lifecycle::observe(player_observer,world_observer);
    const auto remove=reinterpret_cast<std::uintptr_t(*)(void*,void*)>(game+0x68a7e0);
    const auto teardown=reinterpret_cast<std::uintptr_t(*)(void*)>(game+0x68a430);
    send(1,100,3);check(vein_radius::consume(1,clock_value)==3,"old session consumes high sequence");
    send(1,101,2);send(2,100,3);
    DWORD handle=64;check(remove(nullptr,&handle)==71,"disconnect forwards native result");
    check(vein_radius::consume(1,clock_value)==0,"disconnect removes pending activation");
    check(vein_radius::consume(2,clock_value)==3,"disconnect leaves other players untouched");
    for(DWORD generation=2;generation<=101;++generation) {
        send(1,1,2);check(vein_radius::consume(1,clock_value)==2,"new process accepts sequence one immediately");
        send(1,1,3);check(vein_radius::consume(1,clock_value)==0,"duplicate with changed radius stays blocked");
        send(1,100,3);check(vein_radius::consume(1,clock_value)==3,"active connection advances sequence");
        send(1,99);check(vein_radius::consume(1,clock_value)==0,"out-of-order request stays blocked");
        clock_value+=100000;send(1,100);check(vein_radius::consume(1,clock_value)==0,"idle time cannot reset replay history");
        handle=generation<<6;remove(nullptr,&handle);
    }
    send(1,100);send(2,101);check(teardown(nullptr)==72,"world removal forwards native result");
    check(vein_radius::consume(1,clock_value)==0 && vein_radius::consume(2,clock_value)==0,"world removal drops pending requests");
    for(DWORD owner:{1u,2u}){send(owner,1,3);check(vein_radius::consume(owner,clock_value)==3,"new world accepts reset sequences");}
    check(originals==102 && observers==102,"native originals and observers each run exactly once");
    vein_radius::retire_world();
    send(1,100);check(vein_radius::consume(1,clock_value)==1,"seed old connection history");
    std::thread delayed([]{pause_clock=true;send(1,101,3);});
    clock_entered.acquire();
    vein_radius::retire_player(1);
    send(1,1,2);check(vein_radius::consume(1,clock_value)==2,"new connection starts during delayed old receive");
    clock_resume.release();delayed.join();
    check(vein_radius::consume(1,clock_value)==0,"retired in-flight receive cannot restore pending activation");
    send(1,2,2);check(vein_radius::consume(1,clock_value)==2,"retired in-flight receive cannot poison new replay history");
    std::thread delayed_world([]{pause_clock=true;send(1,102,3);});
    clock_entered.acquire();
    vein_radius::retire_world();
    send(1,1,2);check(vein_radius::consume(1,clock_value)==2,"new world starts during delayed old receive");
    clock_resume.release();delayed_world.join();
    check(vein_radius::consume(1,clock_value)==0,"retired world receive cannot restore pending activation");
    send(1,2,2);check(vein_radius::consume(1,clock_value)==2,"retired world receive cannot poison new replay history");
    vein_radius::retire_player(1,true);
    send(1,1000,3);check(vein_radius::consume(1,clock_value)==0,"receive during native player removal stays inactive");
    send(2,2,3);check(vein_radius::consume(2,clock_value)==3,"another player remains active during player removal");
    vein_radius::retire_player(1);
    send(1,1,2);check(vein_radius::consume(1,clock_value)==2,"player removal completion admits a fresh request");
    vein_radius::retire_world(true);
    send(1,1000,3);send(2,1000,3);
    check(vein_radius::consume(1,clock_value)==0 && vein_radius::consume(2,clock_value)==0,"native world teardown blocks all new requests");
    vein_radius::retire_world();
    send(1,1,2);check(vein_radius::consume(1,clock_value)==2,"world teardown completion admits a fresh request");
    for(DWORD encoded:{1u,0xa4000001u,0xa5000001u,0xa7000001u,0xa6c00001u,0xa6000000u,0xa6400000u}) {
        vein_radius::retire_world();
        send_raw(1,encoded);check(vein_radius::consume(1,clock_value)==0,"malformed input rejected without retained replay history");
        send(1,1,2);check(vein_radius::consume(1,clock_value)==2,"fresh nonce one remains usable after malformed input");
    }
    xhl::lifecycle::cancel();
    check(MH_ApplyQueued()==MH_OK && game[0x68a7e0]==0x48 && game[0x68a430]==0x48,
        "cancellation after activation stays disabled through a later apply");
    check(!xhl::lifecycle::prepare(reinterpret_cast<HMODULE>(game)),"failed activation cannot be reused as ready");
    check(MH_Uninitialize()==MH_OK,"remove synthetic hooks");VirtualFree(game,0,MEM_RELEASE);
    std::printf("%u lifecycle regression checks passed\n",checks);return 0;
}
