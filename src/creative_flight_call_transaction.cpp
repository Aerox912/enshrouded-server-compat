#include "creative_flight_call_transaction.hpp"
#include <cstring>
#include <limits>
namespace xhl::creative_flight::call_install::detail {
bool make_plan(std::uintptr_t site,std::uintptr_t slot,std::uintptr_t bridge,Plan& p) noexcept {
    if(!site||site>UINTPTR_MAX-26||!slot||slot%8||slot>UINTPTR_MAX-8||!bridge ||
       (slot>=site&&slot<site+26)||(slot<site&&site-slot<8)||
       (bridge>=site&&bridge<site+26)||(bridge>=slot&&bridge-slot<8))return false;
    auto next=site+6; std::int32_t relative=0;
    if(slot>=next){if(slot-next>INT32_MAX)return false;relative=static_cast<std::int32_t>(slot-next);}
    else{if(next-slot>static_cast<std::uintptr_t>(INT32_MAX)+1)return false;
         relative=static_cast<std::int32_t>(-static_cast<std::int64_t>(next-slot));}
    p={site,slot,bridge,displaced,{0xff,0x15,0,0,0,0,0x90,0x90}};
    std::memcpy(p.after.data()+2,&relative,4);return true;
}
namespace {
unsigned pending(std::span<Thread> threads,std::size_t n) noexcept {
    unsigned count=0;for(std::size_t i=0;i<n;++i)if(threads[i].suspended)++count;return count;
}
Result hold(Recovery& state,std::span<Thread> threads,Result r) noexcept {
    r.pending_suspends=pending(threads,state.opened);r.recovery_required=true;
    if(!r.site_complete){r.published=false;r.original_restored=false;}
    r.backing_retained=true;state.active=true;state.last=r;return r;
}
bool read_owned(const Ops& o,const Recovery& s,std::array<std::uint8_t,8>& bytes) noexcept {
    return o.read(o.context,s.plan.site,bytes)&&
        (bytes==s.plan.before||bytes==s.plan.after||(s.fragment_known&&bytes==s.owned_fragment));
}
bool write_owned(const Ops& o,Recovery& s,const std::array<std::uint8_t,8>& baseline,
                 const std::array<std::uint8_t,8>& desired,std::size_t& count) noexcept {
    count=0;s.modified=true;s.cache_proven=false;
    const bool ok=o.write(o.context,s.plan.site,desired,count);
    s.fragment_known=count<=8;s.owned_fragment=baseline;
    if(s.fragment_known)for(std::size_t i=0;i<count;++i)s.owned_fragment[i]=desired[i];
    return ok&&count==8;
}
Result finish(const Ops& o,Recovery& s,std::span<Thread> threads,Result r) noexcept {
    std::array<std::uint8_t,8> bytes{};
    // This proof is mandatory even after a failed rollback/protection operation.
    // Retained backing is never permission to resume onto mixed/unknown code.
    r.site_complete=o.read(o.context,s.plan.site,bytes)&&
                    (bytes==s.plan.before||bytes==s.plan.after);
    if(!r.site_complete){r.status=s.modified?Status::rollback_failure:Status::proof_failure;
        if(s.protection_known&&!s.protection_proven){std::uint32_t ignored=0;
            s.protection_proven=o.protect(o.context,s.plan.site,8,s.original_protection,ignored);}
        return hold(s,threads,r);}
    if(s.modified&&!s.cache_proven){s.cache_proven=o.flush(o.context,s.plan.site,8);
        if(!s.cache_proven){r.status=Status::cache_failure;return hold(s,threads,r);}}
    if(s.protection_known&&!s.protection_proven){std::uint32_t ignored=0;
        s.protection_proven=o.protect(o.context,s.plan.site,8,s.original_protection,ignored);
        if(!s.protection_proven){r.status=Status::protect_failure;return hold(s,threads,r);}}
    std::array<std::uint8_t,8> final_bytes{};
    if(!o.read(o.context,s.plan.site,final_bytes)||final_bytes!=bytes){
        r.site_complete=false;r.status=Status::rollback_failure;return hold(s,threads,r);}
    r.published=bytes==s.plan.after;
    r.original_restored=s.modified&&bytes==s.plan.before;
    bool resumed=true;
    for(std::size_t i=s.opened;i>0;--i)if(threads[i-1].suspended){
        bool discharged=false;
        for(unsigned attempt=0;attempt<3&&!discharged;++attempt)
            discharged=o.resume(o.context,threads[i-1].handle);
        if(discharged)threads[i-1].suspended=false;else resumed=false;
    }
    if(!resumed){r.status=Status::resume_failure;return hold(s,threads,r);}
    for(std::size_t i=0;i<s.opened;++i){o.close(o.context,threads[i].handle);threads[i].handle=0;}
    s.active=false;r.pending_suspends=0;r.recovery_required=false;s.last=r;return r;
}
}
Result run(const Plan& p,const Ops& o,std::span<Thread> threads,std::span<std::uint32_t> ids,unsigned limit) noexcept {
    Result r;r.status=Status::invalid_request;Plan checked;
    if(o.recovery&&o.recovery->active){r=o.recovery->last;r.status=Status::already_attempted;return r;}
    if(!make_plan(p.site,p.slot,p.bridge,checked)||checked.before!=p.before||checked.after!=p.after||
       threads.empty()||ids.size()<threads.size()||!limit||limit>8||!o.recovery||
       !o.enumerate||!o.open||!o.close||!o.suspend||!o.resume||!o.rip||!o.proof||!o.read||
       !o.protect||!o.write||!o.flush)return r;
    auto& state=*o.recovery;
    for(unsigned attempt=1;attempt<=limit;++attempt){
        state={};state.plan=p;state.owned_fragment=p.before;state.fragment_known=true;
        r={};r.attempts=attempt;std::size_t n=0;bool retry=false;
        if(!o.enumerate(o.context,ids,n)||n>threads.size()){r.status=Status::thread_failure;return r;}
        for(std::size_t i=0;i<n;++i){
            if(!ids[i]){r.status=Status::thread_failure;return r;}
            for(std::size_t j=0;j<i;++j)if(ids[i]==ids[j]){r.status=Status::thread_failure;return r;}
        }
        for(;state.opened<n;++state.opened){threads[state.opened]={ids[state.opened],0,false};
            if(!o.open(o.context,ids[state.opened],threads[state.opened].handle)||!threads[state.opened].handle)break;}
        if(state.opened!=n){
            for(std::size_t i=0;i<state.opened;++i){o.close(o.context,threads[i].handle);threads[i].handle=0;}
            r.status=Status::thread_failure;return r;
        }
        r.status=Status::thread_failure;bool ready=true;
        for(std::size_t i=0;i<n;++i){if(!o.suspend(o.context,threads[i].handle)){ready=false;break;}
            threads[i].suspended=true;}
        if(ready)for(unsigned scan=0;scan<2;++scan){
            std::size_t count=0;
            if(!o.enumerate(o.context,ids,count)||count>ids.size()){ready=false;break;}
            if(count!=n){ready=false;retry=true;r.status=Status::unstable_threads;break;}
            for(std::size_t j=0;j<count&&ready;++j){bool seen=false;
                for(std::size_t i=0;i<n;++i)if(ids[j]==threads[i].id)seen=true;
                for(std::size_t k=0;k<j;++k)if(ids[k]==ids[j])seen=false;
                if(!seen){ready=false;retry=true;r.status=Status::unstable_threads;}
            }
        }
        if(ready)for(std::size_t i=0;i<n;++i){std::uintptr_t ip=0;
            if(!o.rip(o.context,threads[i].handle,ip)||!ip){ready=false;break;}
            if(ip>p.site&&ip<p.site+8){ready=false;r.status=Status::interior_ip;break;}}
        std::array<std::uint8_t,8> bytes{};
        if(ready&&(!o.proof(o.context)||!o.read(o.context,p.site,bytes)||bytes!=p.before)){
            ready=false;r.status=Status::proof_failure;}
        std::uint32_t ignored=0;
        if(ready){state.protection_known=o.protect(o.context,p.site,8,0x40,state.original_protection);
            if(!state.protection_known){ready=false;r.status=Status::protect_failure;}}
        if(ready){
            std::size_t written=0;r.backing_retained=true;
            const bool wrote=write_owned(o,state,p.before,p.after,written);
            const bool read=o.read(o.context,p.site,bytes);
            if(!wrote)r.status=Status::write_failure;
            else if(!read||bytes!=p.after)r.status=Status::verify_failure;
            else if(!(state.cache_proven=o.flush(o.context,p.site,8)))r.status=Status::cache_failure;
            else if(!(state.protection_proven=o.protect(o.context,p.site,8,state.original_protection,ignored)))r.status=Status::protect_failure;
            else{r.status=Status::published;r.published=true;}
            if(!r.published){
                bool rollback=read&&(bytes==p.after||bytes==p.before||
                                     (state.fragment_known&&bytes==state.owned_fragment));
                if(rollback){state.protection_proven=false;rollback=o.protect(o.context,p.site,8,0x40,ignored);}
                std::size_t restored=0;
                if(rollback)rollback=write_owned(o,state,bytes,p.before,restored);
                if(rollback)rollback=o.read(o.context,p.site,bytes)&&bytes==p.before;
                if(rollback)rollback=state.cache_proven=o.flush(o.context,p.site,8);
                state.protection_proven=o.protect(o.context,p.site,8,state.original_protection,ignored);
                rollback=rollback&&state.protection_proven;
                if(!rollback)r.status=Status::rollback_failure;
            }
        }
        r=finish(o,state,threads,r);
        if(r.recovery_required||!retry)return r;
    }
    r.status=Status::unstable_threads;return r;
}
Result recover(const Ops& o,std::span<Thread> threads) noexcept {
    Result invalid;invalid.status=Status::invalid_request;
    if(!o.recovery||!o.recovery->active)return invalid;
    if(o.recovery->opened>threads.size()||!o.read||!o.write||!o.protect||!o.flush||!o.resume||!o.close){
        invalid=o.recovery->last;invalid.status=Status::invalid_request;return invalid;}
    auto& s=*o.recovery;auto r=s.last;std::array<std::uint8_t,8> bytes{};
    if(!read_owned(o,s,bytes)){r.site_complete=false;r.status=Status::rollback_failure;return hold(s,threads,r);}
    if(bytes!=s.plan.before&&bytes!=s.plan.after){
        std::uint32_t ignored=0;s.protection_proven=false;
        if(!s.protection_known||!o.protect(o.context,s.plan.site,8,0x40,ignored)){
            r.status=Status::protect_failure;return hold(s,threads,r);}
        std::size_t count=0;
        if(!write_owned(o,s,bytes,s.plan.before,count)){
            r.status=Status::write_failure;
            // finish() still enforces complete bytes/cache/protection before
            // any resume, even if the backend reports a partial recovery write.
            return finish(o,s,threads,r);
        }
    }
    r.status=Status::recovered;return finish(o,s,threads,r);
}
}
#if defined(_WIN32) && defined(_M_X64)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winternl.h>
#include <bcrypt.h>
#include <new>
#include <atomic>
#include <intrin.h>
#include "creative_flight_call_proof.hpp"
namespace xhl::creative_flight::call_install {
namespace {
using namespace detail;
constexpr std::size_t thread_capacity=4096, query_capacity=16*1024*1024, header_capacity=65536;
using Query=NTSTATUS (NTAPI*)(SYSTEM_INFORMATION_CLASS,PVOID,ULONG,PULONG);
using Version=NTSTATUS (NTAPI*)(PRTL_OSVERSIONINFOW);
static_assert(sizeof(SYSTEM_PROCESS_INFORMATION)==256);
static_assert(offsetof(SYSTEM_PROCESS_INFORMATION,UniqueProcessId)==80);
static_assert(sizeof(SYSTEM_THREAD_INFORMATION)==80);
static_assert(offsetof(SYSTEM_THREAD_INFORMATION,ClientId)==40);
// One process-local writer gate. The caller must additionally serialize ALL
// other code writers; this installer cannot acquire a foreign writer's lock.
volatile LONG writer_gate=0,recovery_gate=0,attempted[2]={0,0};
struct Native {
    Recovery recovery{};
    std::uintptr_t atomic_base=0,site=0;
    std::array<std::uint8_t,16> atomic_before{},atomic_after{};
    Query query=nullptr;
    HANDLE identity_file=nullptr;
    DWORD pid=0,self=0,host_build=0;
    std::uint32_t query_bytes=0,process_records=0,process_threads=0;
    std::uintptr_t base=0,slot=0,bridge=0;
    std::uint32_t image_size=0,header_size=0;
    const ProofBytes* proof_bytes=nullptr;
    std::size_t proof_count=0;
    bool (*fixture_proof)(void*)=nullptr;
    void* fixture_context=nullptr;
    alignas(8) std::uint8_t query_buffer[query_capacity]{};
    std::uint8_t expected_header[header_capacity]{},loaded_header[header_capacity]{};
    Thread threads[thread_capacity]{};
    std::uint32_t ids[thread_capacity]{};
};
std::atomic<Native*> retained{nullptr};
bool supported(Query& query,DWORD& build
#if defined(CREATIVE_CALL_INSTALL_SELFTEST)
    ,bool isolated_test=false
#endif
) noexcept {
    const auto nt=GetModuleHandleW(L"ntdll.dll");
    const auto kernel=GetModuleHandleW(L"kernel32.dll");
    if(!nt||!kernel||GetProcAddress(nt,"wine_get_version"))return false;
    const auto version=reinterpret_cast<Version>(GetProcAddress(nt,"RtlGetVersion"));
    query=reinterpret_cast<Query>(GetProcAddress(nt,"NtQuerySystemInformation"));
    using Machine=BOOL(WINAPI*)(HANDLE,USHORT*,USHORT*);
    const auto machine=reinterpret_cast<Machine>(GetProcAddress(kernel,"IsWow64Process2"));
    if(!version||!query||!machine)return false;
    RTL_OSVERSIONINFOW os{};os.dwOSVersionInfoSize=sizeof(os);
    if(version(&os)!=0)return false;build=os.dwBuildNumber;
    bool known_build=supported_windows_build(build);
#if defined(CREATIVE_CALL_INSTALL_SELFTEST)
    // Test executable only. install() always leaves isolated_test false.
    // The override is absent from normal production object files.
    known_build=known_build||isolated_test;
#endif
    USHORT process=0,native=0;
    return known_build&&os.dwMajorVersion==10&&os.dwMinorVersion==0&&
        machine(GetCurrentProcess(),&process,&native)&&
        process==IMAGE_FILE_MACHINE_UNKNOWN&&native==IMAGE_FILE_MACHINE_AMD64;
}
Native* prepare(
#if defined(CREATIVE_CALL_INSTALL_SELFTEST)
    bool isolated_test=false
#endif
) noexcept {
    Query query=nullptr;DWORD build=0;
    if(!supported(query,build
#if defined(CREATIVE_CALL_INSTALL_SELFTEST)
        ,isolated_test
#endif
    ))return nullptr;
    auto* storage=VirtualAlloc(nullptr,sizeof(Native),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!storage)return nullptr;
    auto* c=::new(storage) Native{}; // Pre-touch all fixed storage before freezing.
    c->query=query;c->host_build=build;c->pid=GetCurrentProcessId();c->self=GetCurrentThreadId();return c;
}
bool enumerate(void* context,std::span<std::uint32_t> ids,std::size_t& count) noexcept {
    auto& c=*static_cast<Native*>(context);count=0;ULONG length=0;
    c.query_bytes=0;c.process_records=0;c.process_threads=0;
    if(c.query(SystemProcessInformation,c.query_buffer,static_cast<ULONG>(query_capacity),&length)!=0||
       length<sizeof(SYSTEM_PROCESS_INFORMATION)||length>query_capacity)return false;
    c.query_bytes=length;std::size_t offset=0;bool found=false;
    for(;;){
        if(offset%8||offset>length||length-offset<sizeof(SYSTEM_PROCESS_INFORMATION))return false;
        const auto* p=reinterpret_cast<const SYSTEM_PROCESS_INFORMATION*>(c.query_buffer+offset);
        ++c.process_records;
        const std::size_t size=p->NextEntryOffset?p->NextEntryOffset:length-offset;
        if(size>length-offset||size<sizeof(*p)||size%8||
           p->NumberOfThreads>(size-sizeof(*p))/sizeof(SYSTEM_THREAD_INFORMATION))return false;
        if(reinterpret_cast<std::uintptr_t>(p->UniqueProcessId)==c.pid){
            if(found||!p->NumberOfThreads)return false;found=true;c.process_threads=p->NumberOfThreads;
            const auto* t=reinterpret_cast<const SYSTEM_THREAD_INFORMATION*>(p+1);
            bool self=false;
            for(ULONG i=0;i<p->NumberOfThreads;++i){
                const auto id=reinterpret_cast<std::uintptr_t>(t[i].ClientId.UniqueThread);
                if(reinterpret_cast<std::uintptr_t>(t[i].ClientId.UniqueProcess)!=c.pid||!id||id>MAXDWORD)return false;
                if(id==c.self){if(self)return false;self=true;continue;}
                if(count>=ids.size())return false;
                ids[count++]=static_cast<std::uint32_t>(id);
            }
            if(!self)return false;
        }
        if(!p->NextEntryOffset)break;
        offset+=size;
    }
    return found;
}
bool open_thread(void* context,std::uint32_t id,std::uintptr_t& out) noexcept {
    auto& c=*static_cast<Native*>(context);out=0;
    const auto h=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_LIMITED_INFORMATION,FALSE,id);
    if(!h)return false;
    if(GetThreadId(h)!=id||GetProcessIdOfThread(h)!=c.pid){CloseHandle(h);return false;}
    out=reinterpret_cast<std::uintptr_t>(h);return true;
}
void close_thread(void*,std::uintptr_t h) noexcept {CloseHandle(reinterpret_cast<HANDLE>(h));}
bool suspend_thread(void*,std::uintptr_t h) noexcept {return SuspendThread(reinterpret_cast<HANDLE>(h))!=MAXDWORD;}
bool resume_thread(void*,std::uintptr_t h) noexcept {
    const DWORD old=ResumeThread(reinterpret_cast<HANDLE>(h));return old!=MAXDWORD&&old>0;
}
bool get_rip(void*,std::uintptr_t h,std::uintptr_t& ip) noexcept {
    CONTEXT c{};c.ContextFlags=CONTEXT_CONTROL;
    if(!GetThreadContext(reinterpret_cast<HANDLE>(h),&c))return false;ip=c.Rip;return true;
}
bool read_memory(void* context,std::uintptr_t address,std::span<std::uint8_t> out) noexcept {
    SIZE_T read=0;
    auto* c=static_cast<Native*>(context);
    if(c&&c->atomic_base&&address==c->site&&out.size()==8){
        std::array<std::uint8_t,16> block{};
        if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(c->atomic_base),
                              block.data(),block.size(),&read)||read!=block.size()||
           (block!=c->atomic_before&&block!=c->atomic_after))return false;
        std::memcpy(out.data(),block.data()+5,8);return true;
    }
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),
                             out.data(),out.size(),&read)&&read==out.size();
}
bool protect_memory(void*,std::uintptr_t address,std::size_t size,std::uint32_t flags,std::uint32_t& old) noexcept {
    DWORD protection=0;const bool ok=VirtualProtect(reinterpret_cast<void*>(address),size,flags,&protection)!=FALSE;
    old=protection;return ok;
}
bool atomic_exchange(std::uintptr_t address,const std::array<std::uint8_t,16>& before,
                     const std::array<std::uint8_t,16>& after) noexcept {
    alignas(16) long long compare[2]{},desired[2]{};
    std::memcpy(compare,before.data(),16);std::memcpy(desired,after.data(),16);
    __try{return _InterlockedCompareExchange128(reinterpret_cast<volatile long long*>(address),
                                                desired[1],desired[0],compare)!=0;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool write_memory(void* context,std::uintptr_t address,std::span<const std::uint8_t> bytes,std::size_t& count) noexcept {
    auto& c=*static_cast<Native*>(context);count=0;
    if(!c.atomic_base||address!=c.site||bytes.size()!=8)return false;
    const bool forward=std::memcmp(bytes.data(),c.atomic_after.data()+5,8)==0;
    const bool backward=std::memcmp(bytes.data(),c.atomic_before.data()+5,8)==0;
    if(!forward&&!backward)return false;
    const bool ok=atomic_exchange(c.atomic_base,forward?c.atomic_before:c.atomic_after,
                                 forward?c.atomic_after:c.atomic_before);
    if(ok)count=8;return ok;
}
bool flush_memory(void*,std::uintptr_t address,std::size_t size) noexcept {
    return FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<const void*>(address),size)!=FALSE;
}
bool prove(void* context) noexcept {
    auto& c=*static_cast<Native*>(context);
    if(c.slot){MEMORY_BASIC_INFORMATION memory{};std::uintptr_t value=0;
        if(VirtualQuery(reinterpret_cast<void*>(c.slot),&memory,sizeof(memory))!=sizeof(memory)||
           memory.State!=MEM_COMMIT||memory.Protect!=PAGE_READONLY||
           !read_memory(nullptr,c.slot,{reinterpret_cast<std::uint8_t*>(&value),sizeof(value)})||value!=c.bridge)return false;}
    if(c.fixture_proof)return c.fixture_proof(c.fixture_context);
    if(!c.base||!c.header_size||!read_memory(nullptr,c.base,{c.loaded_header,c.header_size})||
       std::memcmp(c.loaded_header,c.expected_header,c.header_size)!=0)return false;
    std::array<std::uint8_t,96> current{};
    for(std::size_t i=0;i<c.proof_count;++i){const auto& p=c.proof_bytes[i];
        if(!p.size||p.size>current.size()||p.rva>c.image_size||p.size>c.image_size-p.rva||
           !read_memory(nullptr,c.base+p.rva,{current.data(),p.size})||
           std::memcmp(current.data(),p.bytes,p.size)!=0)return false;
    }
    return true;
}
Ops ops(Native& c) noexcept {
    return {&c,enumerate,open_thread,close_thread,suspend_thread,resume_thread,get_rip,prove,
            read_memory,protect_memory,write_memory,flush_memory,&c.recovery};
}
// An independent allocation, never a presumed code cave. Allocation candidates
// are bounded by rel32, system address limits and allocation granularity.
void* near_slot(std::uintptr_t site,std::uintptr_t bridge) noexcept {
    SYSTEM_INFO system{};GetSystemInfo(&system);
    if(!system.dwAllocationGranularity||!system.dwPageSize)return nullptr;
    const auto gran=static_cast<std::uintptr_t>(system.dwAllocationGranularity);
    const auto next=site+6;
    const auto bottom=next>static_cast<std::uintptr_t>(INT32_MAX)+1?next-static_cast<std::uintptr_t>(INT32_MAX)-1:0;
    const auto top=next>UINTPTR_MAX-INT32_MAX?UINTPTR_MAX:next+INT32_MAX;
    auto low=bottom>reinterpret_cast<std::uintptr_t>(system.lpMinimumApplicationAddress)?bottom:reinterpret_cast<std::uintptr_t>(system.lpMinimumApplicationAddress);
    const auto high=top<reinterpret_cast<std::uintptr_t>(system.lpMaximumApplicationAddress)?top:reinterpret_cast<std::uintptr_t>(system.lpMaximumApplicationAddress);
    if(low>UINTPTR_MAX-(gran-1))return nullptr;
    low=(low+gran-1)&~(gran-1);
    for(auto address=low;address<=high;){
        MEMORY_BASIC_INFORMATION info{};
        if(VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof(info))!=sizeof(info))return nullptr;
        const auto region=reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        if(info.RegionSize>UINTPTR_MAX-region)return nullptr;
        const auto end=region+info.RegionSize;
        if(info.State==MEM_FREE&&end>=address&&end-address>=system.dwPageSize){
            auto* slot=VirtualAlloc(reinterpret_cast<void*>(address),system.dwPageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(slot){
                Plan p;DWORD old=0;std::uintptr_t observed=0;
                SIZE_T count=0;
                const bool ok=make_plan(site,reinterpret_cast<std::uintptr_t>(slot),bridge,p)&&
                    WriteProcessMemory(GetCurrentProcess(),slot,&bridge,sizeof(bridge),&count)&&count==sizeof(bridge)&&
                    ReadProcessMemory(GetCurrentProcess(),slot,&observed,sizeof(observed),&count)&&count==sizeof(observed)&&observed==bridge&&
                    VirtualProtect(slot,system.dwPageSize,PAGE_READONLY,&old);
                if(ok)return slot;
                VirtualFree(slot,0,MEM_RELEASE);return nullptr;
            }
        }
        if(end>UINTPTR_MAX-(gran-1))return nullptr;
        const auto following=(end+gran-1)&~(gran-1);
        if(following<=address)return nullptr;address=following;
    }
    return nullptr;
}
bool sha256(const std::uint8_t* bytes,std::size_t size,const char* expected) noexcept {
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    DWORD object_size=0,result_size=0;void* object=nullptr;std::uint8_t digest[32]{};
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)==0;
    if(ok)ok=BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&object_size),sizeof(object_size),&result_size,0)==0&&result_size==sizeof(object_size)&&object_size>0;
    if(ok){object=VirtualAlloc(nullptr,object_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);ok=object!=nullptr;}
    if(ok)ok=BCryptCreateHash(algorithm,&hash,static_cast<PUCHAR>(object),object_size,nullptr,0,0)==0;
    if(ok)ok=size<=MAXDWORD&&BCryptHashData(hash,const_cast<PUCHAR>(bytes),static_cast<ULONG>(size),0)==0&&BCryptFinishHash(hash,digest,sizeof(digest),0)==0;
    if(ok)for(std::size_t i=0;i<32;++i){const char* hex="0123456789abcdef";
        if(hex[digest[i]>>4]!=expected[2*i]||hex[digest[i]&15]!=expected[2*i+1])ok=false;
    }
    if(hash)BCryptDestroyHash(hash);if(object)VirtualFree(object,0,MEM_RELEASE);
    if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);return ok;
}
// All file parsing happens before freezing. The non-shared-write/delete file
// handle stays open through the transaction. File bytes are never patched.
bool file_identity(Native& c,Image image,HANDLE& file) noexcept {
    wchar_t path[32768]{};const DWORD path_size=GetModuleFileNameW(nullptr,path,32768);
    if(!path_size||path_size>=32768)return false;
    file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){file=nullptr;return false;}
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file,&size)||size.QuadPart<4096||size.QuadPart>512*1024*1024)return false;
    const auto mapping=CreateFileMappingW(file,nullptr,PAGE_READONLY,0,0,nullptr);if(!mapping)return false;
    const auto* data=static_cast<const std::uint8_t*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,0));
    CloseHandle(mapping);if(!data)return false;
    const auto length=static_cast<std::size_t>(size.QuadPart);
    const bool server=image==Image::server;
    bool ok=sha256(data,length,server?"001c1b40ed091d8c1aee583adde3800d7c858ae2c7f4dff54fca2938b2be1637":"af2f5a1227911d8aa06b3908d6bd0211838211cae14ea91099cb57d0df990781");
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(data);
    const IMAGE_NT_HEADERS64* nt=nullptr;
    if(ok)ok=dos->e_magic==IMAGE_DOS_SIGNATURE&&dos->e_lfanew>=sizeof(*dos)&&
        static_cast<std::size_t>(dos->e_lfanew)<=length-sizeof(IMAGE_NT_HEADERS64);
    if(ok){nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(data+dos->e_lfanew);
        ok=nt->Signature==IMAGE_NT_SIGNATURE&&nt->FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64&&
           nt->FileHeader.SizeOfOptionalHeader==sizeof(IMAGE_OPTIONAL_HEADER64)&&
           nt->OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC&&nt->OptionalHeader.ImageBase==0x140000000&&
           nt->FileHeader.TimeDateStamp==(server?0x69fdecc9U:0x6a4236c8U)&&
           nt->OptionalHeader.SizeOfImage==(server?0x1da7000U:0x2da7000U)&&
           nt->OptionalHeader.SizeOfHeaders<=header_capacity&&nt->OptionalHeader.SizeOfHeaders<=length&&
           nt->FileHeader.NumberOfSections>0&&nt->FileHeader.NumberOfSections<=32;
    }
    if(ok){
        const auto* sections=IMAGE_FIRST_SECTION(nt);
        const auto section_offset=reinterpret_cast<const std::uint8_t*>(sections)-data;
        ok=section_offset>=0&&static_cast<std::size_t>(section_offset)<=nt->OptionalHeader.SizeOfHeaders&&
            nt->FileHeader.NumberOfSections*sizeof(IMAGE_SECTION_HEADER)<=nt->OptionalHeader.SizeOfHeaders-static_cast<std::size_t>(section_offset);
        c.base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        c.image_size=nt->OptionalHeader.SizeOfImage;c.header_size=nt->OptionalHeader.SizeOfHeaders;
        if(!c.header_size||c.base>UINTPTR_MAX-c.image_size)ok=false;
        if(ok)std::memcpy(c.expected_header,data,c.header_size);
        c.proof_bytes=server?server_proof:client_proof;
        c.proof_count=server?std::size(server_proof):std::size(client_proof);
        for(std::size_t i=0;i<c.proof_count&&ok;++i){const auto& p=c.proof_bytes[i];bool found=false;
            for(WORD s=0;s<nt->FileHeader.NumberOfSections;++s){const auto& section=sections[s];
                if(p.rva<section.VirtualAddress)continue;
                const auto delta=p.rva-section.VirtualAddress;
                if(delta>section.SizeOfRawData||p.size>section.SizeOfRawData-delta)continue;
                const auto raw=static_cast<std::uint64_t>(section.PointerToRawData)+delta;
                if(raw>length||p.size>length-raw||found){ok=false;break;}
                found=true;if(std::memcmp(data+raw,p.bytes,p.size)!=0)ok=false;
            }
            if(!found)ok=false;
        }
    }
    UnmapViewOfFile(data);return ok&&prove(&c);
}
bool pin_bridge(std::uintptr_t bridge) noexcept {
    MEMORY_BASIC_INFORMATION memory{};DWORD64 unwind_base=0;
    const auto* function=RtlLookupFunctionEntry(bridge,&unwind_base,nullptr);
    if(!function||unwind_base+function->BeginAddress!=bridge||
       VirtualQuery(reinterpret_cast<void*>(bridge),&memory,sizeof(memory))!=sizeof(memory)||
       memory.State!=MEM_COMMIT||memory.Type!=MEM_IMAGE||
       (memory.Protect!=PAGE_EXECUTE_READ&&memory.Protect!=PAGE_EXECUTE))return false;
    HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(bridge),&module))return false;
    // Main EXE is not an unloadable bridge DLL. Matching bridge ABI/route remains
    // the caller's accepted artifact contract, not inferred from arbitrary code.
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
    const auto image=reinterpret_cast<std::uintptr_t>(module);
    const bool dll=module!=GetModuleHandleW(nullptr)&&memory.AllocationBase==module&&
        read_memory(nullptr,image,{reinterpret_cast<std::uint8_t*>(&dos),sizeof(dos)})&&
        dos.e_magic==IMAGE_DOS_SIGNATURE&&dos.e_lfanew>=sizeof(dos)&&dos.e_lfanew<=65536&&
        image<=UINTPTR_MAX-static_cast<std::uintptr_t>(dos.e_lfanew)-sizeof(nt)&&
        read_memory(nullptr,image+dos.e_lfanew,{reinterpret_cast<std::uint8_t*>(&nt),sizeof(nt)})&&
        nt.Signature==IMAGE_NT_SIGNATURE&&nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64&&
        (nt.FileHeader.Characteristics&IMAGE_FILE_DLL)!=0;
    HMODULE pinned=nullptr;
    const bool ok=dll&&GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(bridge),&pinned)&&pinned==module;
    FreeLibrary(module);return ok;
}
bool prepare_atomic(Native& c,std::uintptr_t site,const Plan& plan) noexcept {
    if(site%16!=5)return false;
    int cpu[4]{};__cpuid(cpu,1);
    if((cpu[2]&(1<<13))==0)return false;
    const auto block=site-5;SYSTEM_INFO system{};GetSystemInfo(&system);
    MEMORY_BASIC_INFORMATION memory{};
    if(!system.dwPageSize||block%16||block%system.dwPageSize>system.dwPageSize-16||
       VirtualQuery(reinterpret_cast<void*>(block),&memory,sizeof(memory))!=sizeof(memory)||
       memory.State!=MEM_COMMIT||memory.Type!=MEM_IMAGE||
       memory.Protect!=PAGE_EXECUTE_READ||block<reinterpret_cast<std::uintptr_t>(memory.BaseAddress)||
       block-reinterpret_cast<std::uintptr_t>(memory.BaseAddress)>memory.RegionSize||
       memory.RegionSize-(block-reinterpret_cast<std::uintptr_t>(memory.BaseAddress))<16||
       !read_memory(nullptr,block,c.atomic_before)||
       std::memcmp(c.atomic_before.data()+5,plan.before.data(),8)!=0)return false;
    c.atomic_after=c.atomic_before;std::memcpy(c.atomic_after.data()+5,plan.after.data(),8);
    c.site=site;c.atomic_base=block;return true;
}
Result publish(Native& c,std::uintptr_t site,std::uintptr_t bridge) noexcept {
    Result r;auto* slot=near_slot(site,bridge);
    if(!slot){r.status=Status::allocation_failure;return r;}
    Plan p;
    if(!make_plan(site,reinterpret_cast<std::uintptr_t>(slot),bridge,p))r.status=Status::invalid_request;
    else if(!prepare_atomic(c,site,p))r.status=Status::unavailable;
    else{c.slot=p.slot;c.bridge=p.bridge;r=run(p,ops(c),c.threads,c.ids);}
    if(r.recovery_required||r.pending_suspends)r.backing_retained=true;
    if(!r.backing_retained)VirtualFree(slot,0,MEM_RELEASE);
    return r;
}
void release(Native* c) noexcept {
    if(!c||c->recovery.active)return;
    // A persistent ResumeThread failure retains the thread handle AND bookkeeping
    // rather than silently dropping the installer-owned suspend increment.
    for(const auto& thread:c->threads)if(thread.suspended)return;
    c->~Native();VirtualFree(c,0,MEM_RELEASE);
}
} // namespace
Result install(Image image,std::uintptr_t bridge) noexcept {
    Result r;r.status=Status::invalid_request;
    if((image!=Image::server&&image!=Image::client)||!bridge)return r;
    if(InterlockedCompareExchange(&writer_gate,1,0)!=0){r.status=Status::unavailable;return r;}
    const unsigned index=image==Image::server?0U:1U;
    if(InterlockedCompareExchange(&attempted[index],1,0)!=0)r.status=Status::already_attempted;
    else{
        auto* c=prepare();
        if(!c)r.status=Status::unavailable;
        else{
            HANDLE file=nullptr;
            if(!file_identity(*c,image,file))r.status=Status::identity_failure;
            else if(!pin_bridge(bridge))r.status=Status::invalid_request;
            else{c->identity_file=file;r=publish(*c,c->base+(image==Image::server?0x187595:0x3a60a5),bridge);}
            if(r.recovery_required)retained.store(c,std::memory_order_release);
            else{if(file)CloseHandle(file);release(c);}
        }
    }
    if(!r.recovery_required)InterlockedExchange(&writer_gate,0);return r;
}
Result recover_retained() noexcept {
    Result r;r.status=Status::unavailable;
    if(InterlockedCompareExchange(&recovery_gate,1,0)!=0){r.recovery_required=true;return r;}
    auto* c=retained.load(std::memory_order_acquire);
    if(!c)r.status=Status::invalid_request;
    else if(c->self!=GetCurrentThreadId()){r=c->recovery.last;r.status=Status::invalid_request;}
    else{
        r=detail::recover(ops(*c),c->threads);
        if(!r.recovery_required){
            retained.store(nullptr,std::memory_order_release);
            if(c->identity_file)CloseHandle(c->identity_file);
            release(c);InterlockedExchange(&writer_gate,0);
        }
    }
    InterlockedExchange(&recovery_gate,0);return r;
}
#if defined(CREATIVE_CALL_INSTALL_SELFTEST)
namespace detail {
bool native_atomic_probe(std::uintptr_t address,const std::array<std::uint8_t,16>& before,const std::array<std::uint8_t,16>& after) noexcept {
    int cpu[4]{};__cpuid(cpu,1);MEMORY_BASIC_INFORMATION memory{};
    if((cpu[2]&(1<<13))==0||address%16||
       VirtualQuery(reinterpret_cast<void*>(address),&memory,sizeof(memory))!=sizeof(memory)||
       memory.State!=MEM_COMMIT||memory.Protect!=PAGE_READWRITE)return false;
    return atomic_exchange(address,before,after);
}
bool pin_native_fixture_bridge(std::uintptr_t bridge) noexcept {return bridge&&pin_bridge(bridge);}
bool reject_fixture_file_identity(Image image) noexcept {
    auto* c=prepare(true);if(!c)return false;HANDLE file=nullptr;
    const bool rejected=!file_identity(*c,image,file);if(file)CloseHandle(file);release(c);return rejected;
}
Result publish_native_fixture(std::uintptr_t site,std::uintptr_t bridge,bool (*proof)(void*),void* context) noexcept {
    Result r;if(!proof||!site||!bridge){r.status=Status::invalid_request;return r;}
    auto* c=prepare(true);if(!c)return r;
    c->fixture_proof=proof;c->fixture_context=context;
    r=publish(*c,site,bridge);release(c);return r;
}
bool native_thread_snapshot(std::span<std::uint32_t> ids,std::size_t& size,SnapshotEvidence* evidence) noexcept {
    auto* c=prepare(true);if(!c)return false;
    const bool ok=enumerate(c,ids,size);
    if(evidence)*evidence={c->host_build,c->query_bytes,c->process_records,c->process_threads};
    release(c);return ok;
}
}
#endif
}
#else
namespace xhl::creative_flight::call_install {
Result install(Image,std::uintptr_t) noexcept {return {};}
Result recover_retained() noexcept {return {};}
}
#endif
