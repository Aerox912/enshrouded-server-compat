// SPDX-License-Identifier: MIT
#include "vein_radius.hpp"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace vein_radius {
namespace {
unsigned char* plugin = nullptr;
RadiusProvider local_radius = nullptr;
thread_local unsigned multiplier = 1;
thread_local bool chaining = false;
using Chain = std::uintptr_t (*)(void*, void*, unsigned);
using Input = int (*)(void*, void*);
using Terraform = int (*)(void*, void*, void*);
using Contains = int (*)(float, float, float, const float*);
Chain original_chain = nullptr;
Contains original_contains = nullptr;
Input original_pack = nullptr;
std::atomic<DWORD> next_nonce{0};
constexpr DWORD magic = 0x434c4858;
constexpr DWORD extended = 0xa6000000;
constexpr DWORD nonce_mask = 0x003fffff;
struct Request {
    DWORD sender=0, received=0, nonce=0;
    unsigned radius=1;
    bool pending=false;
};
std::array<Request,64> requests{};
SRWLOCK request_lock = SRWLOCK_INIT;
struct Lock {
    Lock() { AcquireSRWLockExclusive(&request_lock); }
    ~Lock() { ReleaseSRWLockExclusive(&request_lock); }
};
struct RadiusScope {
    unsigned before = multiplier;
    explicit RadiusScope(unsigned value) { multiplier = value; }
    ~RadiusScope() { multiplier = before; }
};
struct ActivationScope {
    DWORD* slot;
    DWORD before;
    ActivationScope(DWORD* state, DWORD value) : slot(state), before(*state) { *slot=value; }
    ~ActivationScope() { *slot=before; }
};
template<class T> T load(const void* base, size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const unsigned char*>(base)+offset, sizeof(value));
    return value;
}
template<class T> void store(void* base, size_t offset, T value) {
    std::memcpy(static_cast<unsigned char*>(base)+offset, &value, sizeof(value));
}
template<class T> bool read(std::uintptr_t address, T& value) {
    if (!address || address > (std::numeric_limits<std::uintptr_t>::max)()-sizeof(T)) return false;
    SIZE_T copied=0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), &value, sizeof(value), &copied)
        && copied==sizeof(value);
}
bool valid_sender(DWORD value) { return value && value!=0xffffffff; }
DWORD now() { return load<DWORD(WINAPI*)()>(plugin,0x4c830)(); }
DWORD* activation() { return reinterpret_cast<DWORD*(*)(void*)>(plugin+0x17880)(plugin+0x37020); }

// Same pinned reflected-playerId route as 1.0.33. Only an explicit zero entity
// selects its context fallback; unreadable/malformed reflection never does.
DWORD sender(void* entity_argument, void* reflection) {
    const auto ref = reinterpret_cast<std::uintptr_t>(reflection);
    std::uintptr_t entity_pointer=0;
    DWORD entity=0;
    if (!ref || ref>(std::numeric_limits<std::uintptr_t>::max)()-0x38 ||
        !read(reinterpret_cast<std::uintptr_t>(entity_argument),entity_pointer) || !read(entity_pointer,entity)) return 0;
    if (!entity) {
        std::uintptr_t context=0;
        if (!read(ref+0x30,context) || !context) return 0;
        for (size_t slot : {size_t(0x49510),size_t(0x49508)}) {
            const auto lookup=load<DWORD(*)(void*)>(plugin,slot);
            if (!lookup) return 0;
            const DWORD found=lookup(reinterpret_cast<void*>(context));
            if (found>=1 && found<=1023) return found;
        }
        return 0;
    }
    std::uintptr_t records=0,components=0;
    if (!read(ref,records) || !read(ref+0x10,components) || !records || !components) return 0;
    const auto offset=static_cast<size_t>(entity & 0xffff)*24+0xe;
    if (records>(std::numeric_limits<std::uintptr_t>::max)()-offset) return 0;
    WORD member=0;
    if (!read(records+offset,member) || components>(std::numeric_limits<std::uintptr_t>::max)()-member) return 0;
    DWORD value=0;
    return read(components+member,value) && valid_sender(value) ? value : 0;
}
void accept(DWORD owner,DWORD nonce,unsigned radius,DWORD tick) {
    Lock guard;
    Request* target=nullptr;
    Request* oldest=&requests[0];
    for (auto& request:requests) {
        if (request.sender==owner) {
            target=&request;
            break;
        }
        if (!request.sender || (oldest->sender && DWORD(tick-request.received)>DWORD(tick-oldest->received))) oldest=&request;
    }
    if (!target) { target=oldest;*target={};target->sender=owner; }
    DWORD& previous=target->nonce;
    const DWORD distance=(nonce-previous)&nonce_mask;
    // Compare decoded serials, not radius bits. Retain the last serial after
    // consumption/expiry so delayed or altered duplicates cannot become fresh.
    // The paired radius protocol keeps its sequence while this sender is cached.
    if (previous && (!distance || distance>(nonce_mask>>1))) return;
    previous=nonce;target->received=tick;target->radius=radius;target->pending=true;
}
unsigned consume(DWORD owner,DWORD tick) {
    Lock guard;
    for (auto& request:requests) if (request.sender==owner && request.pending) {
        request.pending=false;
        return DWORD(tick-request.received)<1000 ? request.radius : 0;
    }
    return 0;
}
int receive(void* context,void* message) {
    if (message && load<BYTE>(message,0x378) && load<DWORD>(message,0x37c)==magic) {
        const DWORD encoded=load<DWORD>(message,0x380);
        const DWORD tag=encoded & 0xff000000;
        const DWORD nonce=encoded & nonce_mask;
        const unsigned radius=((encoded>>22)&3)+1;
        DWORD owner=0;
        const auto address=reinterpret_cast<std::uintptr_t>(message);
        if (tag==extended && nonce && radius<=3 && address>=16 &&
            read(address-16,owner) && valid_sender(owner)) accept(owner,nonce,radius,now());
        // Every XHLC namespace value is stripped, including future/invalid tags.
        store<BYTE>(message,0x378,0);
        store<std::uint64_t>(message,0x37c,0);
    }
    const auto unpack=load<Input>(plugin,0x49538);
    return unpack?unpack(context,message):0;
}
int terraform(void* context,void* entity,void* reflection) {
    const DWORD owner=sender(entity,reflection);
    const unsigned selected=owner?consume(owner,now()):0;
    RadiusScope radius(selected?selected:1);
    ActivationScope active(activation(),selected?1:0);
    const auto original=load<Terraform>(plugin,0x49528);
    return original?original(context,entity,reflection):0;
}
int pack(void* message,void* input) {
    const int result=original_pack(message,input);
    if (message && load<BYTE>(message,0x378) && load<DWORD>(message,0x37c)==magic &&
        (load<DWORD>(message,0x380)&0xff000000)==0xa5000000) {
        const unsigned selected=local_radius?local_radius():1;
        DWORD nonce=(next_nonce.fetch_add(1)+1)&nonce_mask;
        if (!nonce) nonce=(next_nonce.fetch_add(1)+1)&nonce_mask;
        store<DWORD>(message,0x380,extended | ((selected>=1 && selected<=3?selected-1:0)<<22) | nonce);
    }
    return result;
}
std::uintptr_t chain(void* context,void* shape,unsigned flags) {
    const unsigned selected=(*activation()==0 && local_radius)?local_radius():multiplier;
    RadiusScope radius(selected>=1 && selected<=3?selected:1);
    const bool before=chaining;
    chaining=true;
    const auto result=original_chain(context,shape,flags);
    chaining=before;
    return result;
}
std::uintptr_t stock_chain(void* context,void* shape,unsigned flags) {
    const auto original=load<Chain>(plugin,0x49588);
    const auto state=static_cast<unsigned char*>(TlsGetValue(load<DWORD>(plugin,0x370e0)));
    const bool scale=chaining && state && *state && shape && load<float>(shape,0x1c)==5.0f;
    if (scale) store<float>(shape,0x1c,5.0f*multiplier);
    const auto result=original?original(context,shape,flags):0;
    if (scale) store<float>(shape,0x1c,5.0f);
    return result;
}
int contains(float x,float y,float z,const float* center) {
    if (multiplier==1) return original_contains(x,y,z,center);
    if (!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||!std::isfinite(center[0])||
        !std::isfinite(center[1])||!std::isfinite(center[2])) return 0;
    const float dx=x-center[0],dy=y-center[1],dz=z-center[2];
    const float radius=5.0f*multiplier;
    if (std::fabs(dx)>radius || std::fabs(dy)>radius || std::fabs(dz)>radius) return 0;
    return dx*dx+dy*dy+dz*dz<=radius*radius;
}
}

unsigned current_multiplier() { return multiplier; }

bool prepare(HMODULE module,RadiusProvider provider) {
    if (plugin || !module) return false;
    auto p=reinterpret_cast<unsigned char*>(module);
    // The caller verifies the entire normalized file hash. These checks also
    // reject pre-existing hooks. The bounded 64^3 capacity is patched on disk,
    // before loading, not rewritten under running mining threads.
    struct Site { size_t rva; const char* bytes; size_t length; void* callback; void** original; };
    void* unused_input=nullptr;void* unused_terraform=nullptr;
    const Site sites[]={
        {0x25b0,"\x55\x41\x56\x41\x55\x41\x54\x57\x56\x53",10,reinterpret_cast<void*>(chain),reinterpret_cast<void**>(&original_chain)},
        {0x3300,"\x55\x41\x56\x57\x56\x53\x48\x81\xec\x10\x01\x00\x00",13,reinterpret_cast<void*>(receive),&unused_input},
        {0x4bd0,"\x55\x41\x57\x41\x56\x41\x55\x41\x54\x57\x56\x53",12,reinterpret_cast<void*>(terraform),&unused_terraform},
        {0x1d10,"\x45\x31\xc0\x66\x0f\x7e\xc0\x66\x0f\x7e\xc9",11,reinterpret_cast<void*>(contains),reinterpret_cast<void**>(&original_contains)},
        {0x36c0,"\x55\x41\x54\x57\x56\x53\x48\x81\xec\xf0\x00\x00\x00",13,reinterpret_cast<void*>(pack),reinterpret_cast<void**>(&original_pack)}
    };
    const size_t count=provider?std::size(sites):std::size(sites)-1;
    if (load<DWORD>(p,0x3ad1)!=64*64*64 || !load<void*>(p,0x495a8) || !load<void*>(p,0x49588)) return false;
    for (size_t i=0;i<count;++i) if (std::memcmp(p+sites[i].rva,sites[i].bytes,sites[i].length)) return false;
    size_t created=0;
    for (;created<count;++created) if (MH_CreateHook(p+sites[created].rva,sites[created].callback,sites[created].original)!=MH_OK) break;
    bool queued=created==count;
    if (queued) for (size_t i=0;i<count;++i) if (MH_QueueEnableHook(p+sites[i].rva)!=MH_OK) { queued=false;break; }
    if (!queued) {
        for (size_t i=0;i<created;++i) MH_RemoveHook(p+sites[i].rva);
        return false;
    }
    plugin=p;local_radius=provider;
    // Writable pointer slot populated by the verified original initializer.
    InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(p+0x495a8),reinterpret_cast<void*>(stock_chain));
    return true;
}
}
