#include "flight_native.hpp"
#include "flight_hook.hpp"
#include "flight_identity.hpp"
#include "flight_transport.hpp"
#include <MinHook.h>
#include <bcrypt.h>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <mutex>

namespace xhl::flight {
namespace {
std::uintptr_t image;
Logger logger;
std::wstring config_path;
std::mutex guard;
Sessions sessions;
// Copy hook inputs while its stack-owned query is alive. Never dereference the
// saved query pointer later from the receive thread.
struct AuthorizationProbe {
    std::uintptr_t world=0, expected=0;
    bool approved_for_expected=false;
};
AuthorizationProbe authorization_probe;
std::array<std::uintptr_t,16> observed_worlds{};
AuthProofs proofs;
SteamTransport transport;
Extension extension;
std::atomic<bool> running{false};
using One = std::uintptr_t (*)(void*);
using Two = std::uintptr_t (*)(void*,void*);
using Remove = std::uintptr_t (*)(void*,std::uint16_t);
Two original_event, original_player_reset;
One original_receive, original_peer_reset, original_teardown;
Remove original_remove;
using Lock = void (*)(void*);
Lock native_lock, native_unlock;
bool copy_memory(std::uintptr_t address,void* out,std::size_t size) noexcept {
    if(!address || size > UINTPTR_MAX-address)return false;
    __try { std::memcpy(out,reinterpret_cast<const void*>(address),size);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> T field(std::uintptr_t address,std::size_t offset) noexcept {
    T value{};
    if(offset<=UINTPTR_MAX-address)copy_memory(address+offset,&value,sizeof(value));
    return value;
}
std::uint64_t random_number() noexcept {
    std::uint64_t n=0;
    if(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&n),sizeof(n),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)return 0;
    return n;
}
struct PeerLock {
    void* address;
    explicit PeerLock(std::uintptr_t context) : address(reinterpret_cast<void*>(context+0x40)) {native_lock(address);}
    ~PeerLock(){native_unlock(address);}
};
Peer find_peer(std::uintptr_t backend,std::uint64_t steam) {
    PeerLock lock(backend+0xd88);
    for(std::size_t i=0;i<16;++i) {
        const auto handle=field<std::uint16_t>(backend,0xdf8+i*0x118);
        Peer p;
        if(read_peer(copy_memory,backend,handle,p) && p.steam==steam)return p;
    }
    return {};
}
std::uintptr_t event_hook(void* backend,void* event) {
    const auto b=reinterpret_cast<std::uintptr_t>(backend), e=reinterpret_cast<std::uintptr_t>(event);
    if(field<std::uint8_t>(e,0)!=19)return original_event(backend,event);
    const auto steam=field<std::uint64_t>(e,8);
    const auto success=field<std::uint32_t>(e,0x10)==0;
    const auto before=find_peer(b,steam);
    AuthProofs::Pending pending;
    {
        std::lock_guard lock(guard);
        if(!success){proofs.remove(b,before.handle);sessions.remove_peer(b,before.handle);}
        else pending=proofs.begin(before);
    }
    const auto result=original_event(backend,event);
    const auto after=find_peer(b,steam);
    bool accepted=false;
    {
        std::lock_guard lock(guard);
        accepted=proofs.finish(pending,after,success);
    }
    if(accepted && logger)logger("FLIGHT AUTH: observed native successful Steam callback and matching peer generation.");
    return result;
}
std::uintptr_t remove_hook(void* context,std::uint16_t handle) {
    const auto backend=reinterpret_cast<std::uintptr_t>(context)-0xd88;
    {std::lock_guard lock(guard);proofs.remove(backend,handle);sessions.remove_peer(backend,handle);}
    return original_remove(context,handle);
}
std::uintptr_t reset_hook(void* context) {
    const auto backend=reinterpret_cast<std::uintptr_t>(context)-0xd88;
    {std::lock_guard lock(guard);proofs.clear_backend(backend);sessions.clear_backend(backend);}
    return original_peer_reset(context);
}
std::uintptr_t player_reset_hook(void* state,void* player) {
    const auto world=field<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(state),0x1b0);
    const auto handle=field<std::uint32_t>(reinterpret_cast<std::uintptr_t>(player),0);
    {std::lock_guard lock(guard);if(handle)sessions.remove_owner(world,(handle&63)+1);}
    return original_player_reset(state,player);
}
std::uintptr_t teardown_hook(void* state) {
    {std::lock_guard lock(guard);sessions.clear();}
    return original_teardown(state);
}
void capture_owners(std::uintptr_t state) {
    static std::uintptr_t diagnostic_state=0;
    if(diagnostic_state!=state && logger) {
        diagnostic_state=state;char line[160];
        sprintf_s(line,"FLIGHT DEVELOPMENT: state=%p image=%p.",reinterpret_cast<void*>(state),reinterpret_cast<void*>(image));logger(line);
    }
    std::array<Identity,16> ids{};
    std::array<Peer,16> peers{};
    static std::array<int,16> last_stages{};
    for(std::size_t i=0;i<16;++i) {
        OwnerChain chain;
        OwnerStage stage;
        const bool resolved=read_owner_chain(copy_memory,state,image,i,chain,&stage);
        const int observed=static_cast<int>(stage)+1;
        if(last_stages[i]!=observed && logger) {
            last_stages[i]=observed;
            constexpr const char* names[]={"complete","arguments","world","player","machine","session",
                "session player","session machine","machine record","peer","wrapper","backend","backend type"};
            char line[160];sprintf_s(line,"FLIGHT MAPPING: slot %zu: %s.",i,names[static_cast<int>(stage)]);logger(line);
        }
        if(!resolved)continue;
        PeerLock lock(chain.peer_context);
        if(!read_peer(copy_memory,chain.identity.backend,chain.identity.peer,peers[i]))continue;
        ids[i]=chain.identity;ids[i].steam=peers[i].steam;
    }
    std::lock_guard lock(guard);
    for(std::size_t i=0;i<16;++i)ids[i].authentication=proofs.serial(peers[i]);
    sessions.observe(ids,GetTickCount64());
    for(std::size_t i=0;i<16;++i)observed_worlds[i]=ids[i].world;
    static unsigned mapped=0;
    unsigned next=0;
    for(std::size_t i=0;i<16;++i)if(ids[i].valid(i))next|=1u<<i;
    if(next!=mapped && logger){mapped=next;logger(next?"FLIGHT IDENTITY: authenticated game owner mapping observed.":"FLIGHT IDENTITY: no authenticated game owners remain.");}
}
bool approved_sender(std::uint64_t steam) {
    // Session acceptance alone grants nothing. Only a current allowlisted game
    // identity can reach the protocol; actual activation needs its challenge.
    std::lock_guard lock(guard);
    return sessions.is_approved(steam,GetTickCount64());
}
std::uintptr_t receive_hook(void* state) {
    const auto result=original_receive(state);
    capture_owners(reinterpret_cast<std::uintptr_t>(state));
    static std::uint64_t probe_time=0,probe_calls=0;
    const auto now=GetTickCount64();
    if(now-probe_time>=1000) {
        probe_time=now;const auto probe=glide_probe();
        if(probe.calls!=probe_calls && logger) {
            probe_calls=probe.calls;char line[512];AuthorizationProbe auth;
            {std::lock_guard lock(guard);auth=authorization_probe;}
            sprintf_s(line,"FLIGHT GLIDE: calls=%llu allowed=%llu owner=%u world_match=%u approved_for_expected=%u pitch=%.4f velocity_y=%.4f.",
                probe.calls,probe.allowed,probe.owner,auth.world && auth.world==auth.expected?1u:0u,auth.approved_for_expected?1u:0u,probe.desired_pitch,probe.vertical_velocity);logger(line);
        }
    }
    if(running.load(std::memory_order_acquire) && transport.initialize(true,approved_sender)) {
        static bool initialized=false;
        if(!initialized && logger){initialized=true;logger("FLIGHT TRANSPORT: Steam server channel available.");}
        transport.poll([](std::uint64_t sender,const Message& message) {
            static bool received=false;
            if(!received && logger){received=true;logger("FLIGHT TRANSPORT: received a versioned request on the mod channel.");}
            std::optional<Message> response;
            const auto token=message.kind==Kind::hello?random_number():0;
            bool changed=false;
            {
                std::lock_guard lock(guard);const auto now=GetTickCount64();
                const bool previous=sessions.flying_for(sender,now);
                response=sessions.receive(sender,message,now,token);
                changed=previous!=sessions.flying_for(sender,now);
            }
            if(changed && logger)logger(response && response->enabled?"FLIGHT REQUEST: authenticated owner enabled flight.":"FLIGHT REQUEST: owner flight disabled.");
            if(response)transport.send(sender,*response);
        });
    }
    if(running.load(std::memory_order_acquire) && extension.poll)extension.poll();
    return result;
}
bool authorize(const void* query,std::uint32_t owner) noexcept {
    if(!running.load(std::memory_order_acquire))return false;
    std::uintptr_t world=0;
    read_query_world(copy_memory,reinterpret_cast<std::uintptr_t>(query),world);
    std::unique_lock lock(guard);
    authorization_probe={};authorization_probe.world=world;
    if(owner>=1 && owner<=observed_worlds.size()) {
        authorization_probe.expected=observed_worlds[owner-1];
        authorization_probe.approved_for_expected=sessions.can_fly(authorization_probe.expected,owner,GetTickCount64());
    }
    const bool enabled=sessions.can_fly(world,owner,GetTickCount64());
    lock.unlock(); // Extensions may consult authenticated_owner; never recurse under guard.
    return enabled || (extension.can_fly && extension.can_fly(world,owner));
}
DWORD WINAPI read_config(void*) {
    while(running.load(std::memory_order_acquire)) {
        Allowlist list;
        const auto file=CreateFileW(config_path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file!=INVALID_HANDLE_VALUE) {
            LARGE_INTEGER size{};std::array<char,8192> bytes{};DWORD count=0;
            if(GetFileSizeEx(file,&size) && size.QuadPart>=0 && size.QuadPart<=8192 &&
                ReadFile(file,bytes.data(),static_cast<DWORD>(size.QuadPart),&count,nullptr) && count==size.QuadPart)
                list=parse_allowlist({bytes.data(),count});
            CloseHandle(file);
        }
        {std::lock_guard lock(guard);sessions.configure(list,GetTickCount64());}
        Sleep(500);
    }
    return 0;
}
struct Site {DWORD rva;std::array<unsigned char,16> bytes;void* detour;void** original;};
}
std::optional<Identity> authenticated_identity(std::uint64_t steam) {
    std::lock_guard lock(guard);return sessions.identity_for(steam,GetTickCount64());
}
std::optional<Identity> authenticated_owner(std::uintptr_t world,std::uint32_t owner) {
    std::lock_guard lock(guard);return sessions.identity_for(world,owner,GetTickCount64());
}
bool start_runtime(HMODULE game,const std::wstring& directory,Logger log,Extension added) {
    static bool attempted=false;if(attempted)return false;attempted=true;
    if(!validate_server(game))return false;
    image=reinterpret_cast<std::uintptr_t>(game);logger=log;
    extension=added;
    config_path=directory+L"\\flight-allowlist.cfg";
    native_lock=reinterpret_cast<Lock>(image+0x4ddb70);native_unlock=reinterpret_cast<Lock>(image+0x4f31e0);
    const Site sites[]={
        {0x903880,{0x40,0x55,0x53,0x57,0x48,0x8d,0xac,0x24,0x40,0xf0,0xff,0xff,0xb8,0xc0,0x10,0x00},reinterpret_cast<void*>(event_hook),reinterpret_cast<void**>(&original_event)},
        {0x8f7a60,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x66,0x89,0x54,0x24,0x10,0x57},reinterpret_cast<void*>(remove_hook),reinterpret_cast<void**>(&original_remove)},
        {0x8f7800,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57},reinterpret_cast<void*>(reset_hook),reinterpret_cast<void**>(&original_peer_reset)},
        {0x69e570,{0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57},reinterpret_cast<void*>(receive_hook),reinterpret_cast<void**>(&original_receive)},
        {0x68a7e0,{0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x81,0xec,0x90,0,0,0,0x0f,0xb6,0x82},reinterpret_cast<void*>(player_reset_hook),reinterpret_cast<void**>(&original_player_reset)},
        {0x68a430,{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57},reinterpret_cast<void*>(teardown_hook),reinterpret_cast<void**>(&original_teardown)}
    };
    for(const auto& s:sites)if(std::memcmp(reinterpret_cast<void*>(image+s.rva),s.bytes.data(),s.bytes.size()))return false;
    const auto status=MH_Initialize();if(status!=MH_OK && status!=MH_ERROR_ALREADY_INITIALIZED)return false;
    std::size_t prepared=0;
    for(const auto& s:sites) {
        if(MH_CreateHook(reinterpret_cast<void*>(image+s.rva),s.detour,s.original)!=MH_OK) {
            for(std::size_t i=0;i<prepared;++i)MH_RemoveHook(reinterpret_cast<void*>(image+sites[i].rva));return false;
        }
        ++prepared;
    }
    // Pin before any code can enter a detour. No trampoline is freed after use.
    HMODULE pinned=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN|GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(authorize),&pinned))return false;
    bool enabled=true;
    for(const auto& s:sites)enabled=MH_QueueEnableHook(reinterpret_cast<void*>(image+s.rva))==MH_OK && enabled;
    enabled=enabled && MH_ApplyQueued()==MH_OK;
    if(!enabled || !install(game,authorize,log)) {
        for(const auto& s:sites)MH_DisableHook(reinterpret_cast<void*>(image+s.rva));return false;
    }
    running.store(true,std::memory_order_release);
    const auto worker=CreateThread(nullptr,0,read_config,nullptr,0,nullptr);
    if(!worker){running.store(false);return false;}CloseHandle(worker);
    if(log)log("FLIGHT DEVELOPMENT RUNTIME: native identity and Steam channel installed; live acceptance remains unverified.");
    return true;
}
}
