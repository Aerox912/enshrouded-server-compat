#include <vector>
#include <flight_patch.h>
#include "flight_client_host.hpp"
#include "adapter.hpp"
#include "flight_identity.hpp"
#include <MinHook.h>
#include <bcrypt.h>
#include <mutex>
namespace xhl::client {
using namespace xhl::flight;
std::mutex host_guard;
Host host;
std::uint64_t generation=1;
using Send = bool (*)(void*,std::uint16_t,const void*,std::uint32_t,std::uint8_t,std::uint16_t);
using Remove = std::uintptr_t (*)(void*,std::uint16_t);
using Reset = std::uintptr_t (*)(void*);
using Lock = void (*)(void*);
Send original_send;
Remove original_remove;
Reset original_reset;
Lock native_lock,native_unlock;
Flight::Patch patch;

bool copy_memory(std::uintptr_t address,void* out,std::size_t size) noexcept {
    if(!address || size>UINTPTR_MAX-address)return false;
    __try{std::memcpy(out,reinterpret_cast<void*>(address),size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void forget(void* context,std::uint16_t peer=0) {
    std::lock_guard lock(host_guard);
    if(host.context==reinterpret_cast<std::uintptr_t>(context) && (!peer || host.peer==peer)){host={};++generation;}
}
bool send_hook(void* context,std::uint16_t handle,const void* data,std::uint32_t size,std::uint8_t flags,std::uint16_t lane) {
    const auto result=original_send(context,handle,data,size,flags,lane);
    if(result) {
        const auto address=reinterpret_cast<std::uintptr_t>(context);
        Peer peer;
        native_lock(reinterpret_cast<void*>(address+0x40));
        const bool found=read_peer(copy_memory,address-0xd88,handle,peer);
        native_unlock(reinterpret_cast<void*>(address+0x40));
        const auto type=(peer.steam>>52)&15;
        if(found && !peer.hosting && (peer.steam>>56)==1 && (type==3 || type==4)) {
            std::lock_guard lock(host_guard);
            if(host.context!=address || host.peer!=handle || host.steam!=peer.steam)++generation;
            host={address,handle,peer.steam,generation,GetTickCount64()};
        }
    }
    return result;
}
std::uintptr_t remove_hook(void* context,std::uint16_t handle){forget(context,handle);return original_remove(context,handle);}
std::uintptr_t reset_hook(void* context){forget(context);return original_reset(context);}
Host current_host() {
    std::lock_guard lock(host_guard);
    const auto now=GetTickCount64();
    return host.steam && now>=host.seen && now-host.seen<2000?host:Host{};
}
bool allowed_host(std::uint64_t steam){return steam && current_host().steam==steam;}
std::uint64_t random_number(){std::uint64_t n=0;return BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&n),sizeof(n),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0?n:0;}
bool prepare() {
    const auto game=GetModuleHandleW(nullptr);const auto image=reinterpret_cast<std::uintptr_t>(game);
    if(!xhl::verify_file(xhl::module_path(game),"af2f5a1227911d8aa06b3908d6bd0211838211cae14ea91099cb57d0df990781"))return false;
    struct Site{std::uintptr_t rva;std::array<unsigned char,16> bytes;void* hook;void** original;};
    const Site sites[]={
        {0xe71740,{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x83},reinterpret_cast<void*>(send_hook),reinterpret_cast<void**>(&original_send)},
        {0xe638f0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x66,0x89,0x54,0x24,0x10,0x57},reinterpret_cast<void*>(remove_hook),reinterpret_cast<void**>(&original_remove)},
        {0xe63690,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57},reinterpret_cast<void*>(reset_hook),reinterpret_cast<void**>(&original_reset)}
    };
    for(const auto& s:sites)if(std::memcmp(reinterpret_cast<void*>(image+s.rva),s.bytes.data(),s.bytes.size()))return false;
    if(!patch.prepare(Flight::FindTarget(reinterpret_cast<const std::uint8_t*>(game))))return false;
    native_lock=reinterpret_cast<Lock>(image+0x7ca7e0);native_unlock=reinterpret_cast<Lock>(image+0x7dfaa0);
    auto status=MH_Initialize();if(status!=MH_OK && status!=MH_ERROR_ALREADY_INITIALIZED)return false;
    std::size_t count=0;
    for(const auto& s:sites){
        if(MH_CreateHook(reinterpret_cast<void*>(image+s.rva),s.hook,s.original)!=MH_OK){
            for(std::size_t i=0;i<count;++i)MH_RemoveHook(reinterpret_cast<void*>(image+sites[i].rva));return false;
        }++count;
    }
    HMODULE pinned;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(send_hook),&pinned))return false;
    bool ok=true;
    for(const auto& s:sites)ok=MH_QueueEnableHook(reinterpret_cast<void*>(image+s.rva))==MH_OK && ok;
    ok=ok && MH_ApplyQueued()==MH_OK && patch.install();
    if(!ok)for(const auto& s:sites)MH_DisableHook(reinterpret_cast<void*>(image+s.rva));
    patch.enable(false);return ok;
}

void set_local_flight(bool enabled){patch.enable(enabled);}
}
