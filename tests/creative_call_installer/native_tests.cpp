#include "creative_flight_call_transaction.hpp"
#include <Windows.h>
#include <TlHelp32.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace xhl::creative_flight::call_install;
using namespace xhl::creative_flight::call_install::detail;
extern "C" void creative_g_installer_fixture(std::uintptr_t*,void*) noexcept(false);
extern "C" unsigned char creative_g_installer_site[];
extern "C" void creative_g_dispatch_bridge() noexcept(false);
namespace {
int checks=0,calls=0,mode=0;HANDLE stop_event=nullptr;
std::array<std::uintptr_t,0x138/8> expected{};
struct Snapshot{std::array<std::uint64_t,16> gpr{};std::array<std::array<unsigned char,16>,16> xmm{};std::uint32_t mxcsr=0,padding=0;std::uint64_t flags=0;};
static_assert(sizeof(Snapshot)==0x190);
void check(bool ok,const char* label){++checks;if(!ok)throw std::runtime_error(label);}
DWORD WINAPI idle(void*){WaitForSingleObject(stop_event,INFINITE);return 0;}
DWORD seh_run(std::uintptr_t* row,void* snapshots){__try{creative_g_installer_fixture(row,snapshots);return 0;}__except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}}
bool proof(void*){return std::memcmp(creative_g_installer_site,displaced.data(),8)==0;}
}
extern "C" void creative_g_fixture_prepare(std::uintptr_t*,void*){}
extern "C" void creative_g_fixture_complete(std::uintptr_t*){}
extern "C" void creative_g_dispatch_route(std::uintptr_t* row,void* second) noexcept(false){
    ++calls;if(std::memcmp(row,expected.data(),0x138)!=0||*static_cast<std::uintptr_t*>(second)!=0xabc)throw std::runtime_error("bridge native frame arguments drifted");
    if(mode==1)throw std::runtime_error("intentional published CALL C++ unwind");
    if(mode==2)RaiseException(0xe0470010,0,0,nullptr);
}
int main(){try{
    check(supported_windows_build(19041)&&supported_windows_build(26100)&&supported_windows_build(26300),"reviewed Windows build allowlist endpoints");
    check(!supported_windows_build(19040)&&!supported_windows_build(26101)&&!supported_windows_build(26299)&&!supported_windows_build(26301),"adjacent unsupported builds fail closed");
    std::array<unsigned char,0x100> dynamic{};expected.fill(0x12345678);expected[0x98/8]=reinterpret_cast<std::uintptr_t>(dynamic.data());
    std::array<Snapshot,2> snapshots{};dynamic[0x3d]=4;
    check(proof(nullptr),"fixture starts with exact displaced bytes");creative_g_installer_fixture(expected.data(),snapshots.data());
    check(!calls&&snapshots[1].gpr[1]==4,"original code executes before publication");
    // This process is an isolated test EXE, so the production identity gate
    // must reject it before code publication or bridge pinning.
    const auto rejected=install(Image::server,reinterpret_cast<std::uintptr_t>(&creative_g_dispatch_bridge));
    check((rejected.status==Status::identity_failure||rejected.status==Status::unavailable)&&!rejected.backing_retained&&proof(nullptr),"production pinned game-file identity fails closed on fixture");
    check(install(Image::server,reinterpret_cast<std::uintptr_t>(&creative_g_dispatch_bridge)).status==Status::already_attempted,"production install is one-shot");
    check(install(static_cast<Image>(9),1).status==Status::invalid_request,"invalid image rejected");
    check(reject_fixture_file_identity(Image::server)&&reject_fixture_file_identity(Image::client),"full SHA256 file identity gate rejects fixture as either pinned game image");
    check(!pin_native_fixture_bridge(0)&&!pin_native_fixture_bridge(reinterpret_cast<std::uintptr_t>(&creative_g_dispatch_bridge)),"PIN rejects null and main-EXE bridge");
    const auto bridge_dll=LoadLibraryW(L"bridge_dll_fixture.dll");check(bridge_dll!=nullptr,"isolated bridge DLL loads");
    const auto dll_bridge=GetProcAddress(bridge_dll,"creative_g_dispatch_bridge");check(dll_bridge!=nullptr,"isolated DLL exposes accepted unwind bridge");
    check(pin_native_fixture_bridge(reinterpret_cast<std::uintptr_t>(dll_bridge)),"production DLL bridge PIN succeeds");
    check(FreeLibrary(bridge_dll)!=FALSE&&GetModuleHandleW(L"bridge_dll_fixture.dll")==bridge_dll,"pinned bridge DLL remains loaded after releasing caller reference");
    check(recover_retained().status==Status::invalid_request,"no retained production transaction can be recovered");
    auto* atomic=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    check(atomic!=nullptr,"isolated aligned atomic test storage");
    std::array<std::uint8_t,16> atomic_before{},atomic_after{};
    for(std::size_t i=0;i<16;++i)atomic_before[i]=static_cast<std::uint8_t>(0x30+i);
    atomic_after=atomic_before;for(std::size_t i=5;i<13;++i)atomic_after[i]=static_cast<std::uint8_t>(0xa0+i);
    std::memcpy(atomic,atomic_before.data(),16);
    check(native_atomic_probe(reinterpret_cast<std::uintptr_t>(atomic),atomic_before,atomic_after)&&std::memcmp(atomic,atomic_after.data(),16)==0,"native CAS128 installs whole block preserving all eight neighbor bytes");
    for(auto neighbor:{0U,1U,2U,3U,4U,13U,14U,15U}){auto wrong_neighbor=atomic_after;wrong_neighbor[neighbor]^=1;
        check(!native_atomic_probe(reinterpret_cast<std::uintptr_t>(atomic),wrong_neighbor,atomic_before)&&std::memcmp(atomic,atomic_after.data(),16)==0,"CAS128 mismatch at each neighboring byte changes zero bytes");}
    check(native_atomic_probe(reinterpret_cast<std::uintptr_t>(atomic),atomic_after,atomic_before)&&std::memcmp(atomic,atomic_before.data(),16)==0,"native atomic rollback restores whole original block");
    check(!native_atomic_probe(reinterpret_cast<std::uintptr_t>(atomic)+1,atomic_before,atomic_after),"unaligned CAS128 probe rejected before intrinsic");
    VirtualFree(atomic,0,MEM_RELEASE);
    const auto atomic_site=reinterpret_cast<std::uintptr_t>(creative_g_installer_site);
    check(atomic_site%16==5,"native fixture matches both pinned sites' offset-five atomic block");
    std::array<std::uint8_t,16> surrounding{};std::memcpy(surrounding.data(),creative_g_installer_site-5,16);
    stop_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);check(stop_event!=nullptr,"worker stop event");
    DWORD first_id=0,second_id=0;
    const auto first=CreateThread(nullptr,0,idle,nullptr,0,&first_id),second=CreateThread(nullptr,0,idle,nullptr,0,&second_id);
    check(first&&second,"two real fixture worker threads");
    std::array<std::uint32_t,4096> ids{};std::size_t n=0;SnapshotEvidence evidence{};
    check(native_thread_snapshot(ids,n,&evidence),"checked native Nt snapshot succeeds with isolated test OS override");
    if(!supported_windows_build(evidence.windows_build))check(rejected.status==Status::unavailable,"production unavailable on unreviewed host build");
    else check(rejected.status==Status::identity_failure,"supported production host still rejects unpinned fixture image");
    check(evidence.bytes>0&&evidence.bytes<=16*1024*1024&&evidence.process_records>0&&evidence.process_threads==n+1,"all Nt record bounds/alignment and current process identities checked");
    std::sort(ids.begin(),ids.begin()+n);
    for(unsigned scan=0;scan<3;++scan){
        std::array<std::uint32_t,4096> independent{},again{};std::size_t win32_count=0,nt_count=0;
        const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);check(snapshot!=INVALID_HANDLE_VALUE,"independent Win32 snapshot outside frozen interval");
        THREADENTRY32 entry{};entry.dwSize=sizeof(entry);check(Thread32First(snapshot,&entry)!=FALSE,"independent first thread");
        do{if(entry.th32OwnerProcessID==GetCurrentProcessId()&&entry.th32ThreadID!=GetCurrentThreadId()){check(win32_count<independent.size(),"independent capacity");independent[win32_count++]=entry.th32ThreadID;}entry.dwSize=sizeof(entry);}while(Thread32Next(snapshot,&entry));
        check(GetLastError()==ERROR_NO_MORE_FILES,"independent enumeration completes without failure");CloseHandle(snapshot);
        check(native_thread_snapshot(again,nt_count),"repeat checked native scan");
        std::sort(independent.begin(),independent.begin()+win32_count);std::sort(again.begin(),again.begin()+nt_count);
        check(win32_count==n&&nt_count==n&&std::equal(ids.begin(),ids.begin()+n,independent.begin())&&std::equal(ids.begin(),ids.begin()+n,again.begin()),"three stable Nt rescans agree with independent Win32 identities");
    }
    std::cout<<"host="<<evidence.windows_build<<" Nt bytes="<<evidence.bytes<<" checked process records="<<evidence.process_records<<" fixture process threads="<<evidence.process_threads<<" independent stable scans=3\n";
    bool a=false,b=false,self=false;for(std::size_t i=0;i<n;++i){a|=ids[i]==first_id;b|=ids[i]==second_id;self|=ids[i]==GetCurrentThreadId();}
    check(a&&b&&!self,"snapshot includes workers and excludes installer thread");
    const auto published=publish_native_fixture(reinterpret_cast<std::uintptr_t>(creative_g_installer_site),reinterpret_cast<std::uintptr_t>(&creative_g_dispatch_bridge),proof,nullptr);
    check(published.status==Status::published&&published.published&&published.backing_retained&&!published.pending_suspends&&!published.recovery_required&&published.site_complete,"production transaction publishes isolated actual CALL");
    check(std::memcmp(creative_g_installer_site-5,surrounding.data(),5)==0&&std::memcmp(creative_g_installer_site+8,surrounding.data()+13,3)==0,"actual publication preserves every neighboring instruction byte");
    check(SetEvent(stop_event)!=FALSE,"signal thawed workers");
    const HANDLE workers[]={first,second};check(WaitForMultipleObjects(2,workers,TRUE,5000)==WAIT_OBJECT_0,"both real suspended workers resumed");
    CloseHandle(first);CloseHandle(second);CloseHandle(stop_event);
    const auto site=reinterpret_cast<std::uintptr_t>(creative_g_installer_site);std::int32_t rel=0;
    check(creative_g_installer_site[0]==0xff&&creative_g_installer_site[1]==0x15&&creative_g_installer_site[6]==0x90&&creative_g_installer_site[7]==0x90,"published instruction is CALL indirect plus 2 NOP");
    std::memcpy(&rel,creative_g_installer_site+2,4);const auto slot=site+6+static_cast<std::intptr_t>(rel);
    check(slot%8==0&&*reinterpret_cast<std::uintptr_t*>(slot)==reinterpret_cast<std::uintptr_t>(&creative_g_dispatch_bridge),"real aligned rel32 slot holds bridge address");
    MEMORY_BASIC_INFORMATION slot_memory{},site_memory{};
    check(VirtualQuery(reinterpret_cast<void*>(slot),&slot_memory,sizeof(slot_memory))==sizeof(slot_memory)&&slot_memory.Protect==PAGE_READONLY,"published slot read-only");
    check(VirtualQuery(creative_g_installer_site,&site_memory,sizeof(site_memory))==sizeof(site_memory)&&site_memory.Protect==PAGE_EXECUTE_READ,"fixture code restored execute-read");
    for(unsigned state=0;state<256;++state){dynamic[0x3d]=static_cast<unsigned char>(state);snapshots={};creative_g_installer_fixture(expected.data(),snapshots.data());
        check(snapshots[1].gpr[0]==expected[0x98/8]&&snapshots[1].gpr[1]==state,"real published CALL emulates displaced MOVs for every state");
        for(unsigned reg=2;reg<16;++reg)check(snapshots[0].gpr[reg]==snapshots[1].gpr[reg],"native CALL bridge preserves other registers");
        check(snapshots[0].xmm==snapshots[1].xmm&&snapshots[0].mxcsr==snapshots[1].mxcsr&&snapshots[0].flags==snapshots[1].flags,"native CALL preserves XMM, MXCSR and flags");
    }
    check(calls==256,"every patched execution reaches bridge");
    mode=1;bool caught=false;try{creative_g_installer_fixture(expected.data(),snapshots.data());}catch(const std::runtime_error&){caught=true;}
    check(caught,"actual patched CALL supports C++ unwind through fixture native frame");
    mode=2;check(seh_run(expected.data(),snapshots.data())==0xe0470010,"actual patched CALL supports OS SEH unwind");
    mode=0;creative_g_installer_fixture(expected.data(),snapshots.data());check(calls==259,"bridge executes after both unwinds");
    std::cout<<checks<<" native publication/unwind checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"check "<<checks<<": "<<e.what()<<'\n';return 1;}}
