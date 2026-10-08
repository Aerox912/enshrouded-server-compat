#pragma once
#include "flight_session.hpp"
#include "callback_gate.hpp"
#include <windows.h>
#include <isteamnetworkingmessages.h>

namespace xhl::flight {
// A dedicated Steam Messages channel keeps mod packets away from the game's
// chat and packet parser. Peer Steam identities still have to match a current,
// independently authenticated game session in the server adapter.
class SteamTransport {
    const int channel;
    ISteamNetworkingMessages* api_ = nullptr;
    using Register = void (*)(CCallbackBase*,int);
    using Unregister = void (*)(CCallbackBase*);
    Unregister unregister_ = nullptr;
    class Request final : public CCallbackBase {
        struct State {ISteamNetworkingMessages* api=nullptr;bool (*allowed)(std::uint64_t)=nullptr;};
        CallbackGate<State> gate_;
    public:
        void configure(ISteamNetworkingMessages* api,bool (*allowed)(std::uint64_t)) {gate_.configure({api,allowed});}
        void disable() {gate_.disable();}
        void server_flag(bool server) { m_nCallbackFlags = server ? k_ECallbackFlagsGameServer : 0; }
        void Run(void* p) override {
            const auto* request = static_cast<const SteamNetworkingMessagesSessionRequest_t*>(p);
            const auto id = request->m_identityRemote.GetSteamID64();
            gate_.invoke([&](const State& state) {
                if (state.api && state.allowed && id && state.allowed(id))
                    state.api->AcceptSessionWithUser(request->m_identityRemote);
            });
        }
        void Run(void* p,bool failure,SteamAPICall_t) override { if(!failure) Run(p); }
        int GetCallbackSizeBytes() override { return sizeof(SteamNetworkingMessagesSessionRequest_t); }
    };
    // Retained deliberately for the pinned DLL lifetime. Unregistering does not
    // prove a callback selected on another thread has finished. No owner pointer
    // survives here, and shutdown drains the gate and clears all callable state.
    Request* callback_=nullptr;
public:
    explicit SteamTransport(int selected_channel = 18357) : channel(selected_channel) {}
    SteamTransport(const SteamTransport&)=delete;
    SteamTransport& operator=(const SteamTransport&)=delete;
    ~SteamTransport(){shutdown();}
    bool initialize(bool server, bool (*allowed)(std::uint64_t)) {
        if (api_) return true;
        const auto module = GetModuleHandleW(L"steam_api64.dll");
        if (!module) return false;
        using Accessor = ISteamNetworkingMessages* (*)();
        auto get = reinterpret_cast<Accessor>(GetProcAddress(module,server ?
            "SteamAPI_SteamGameServerNetworkingMessages_SteamAPI_v002" : "SteamAPI_SteamNetworkingMessages_SteamAPI_v002"));
        auto reg = reinterpret_cast<Register>(GetProcAddress(module,"SteamAPI_RegisterCallback"));
        unregister_ = reinterpret_cast<Unregister>(GetProcAddress(module,"SteamAPI_UnregisterCallback"));
        if (!get || !reg || !unregister_) return false;
        api_ = get(); if(!api_)return false;
        if(!callback_)callback_=new Request;
        callback_->configure(api_,allowed);callback_->server_flag(server);
        reg(callback_,SteamNetworkingMessagesSessionRequest_t::k_iCallback);return true;
    }
    bool send(std::uint64_t peer,const Message& message) {
        const auto bytes=encode(message);
        return send_bytes(peer,bytes);
    }
    bool send_bytes(std::uint64_t peer,std::span<const std::uint8_t> bytes) {
        if(bytes.empty() || bytes.size()>1024)return false;
        if(!api_ || !peer)return false;
        SteamNetworkingIdentity identity{};identity.SetSteamID64(peer);
        return api_->SendMessageToUser(identity,bytes.data(),static_cast<uint32>(bytes.size()),
            k_nSteamNetworkingSend_ReliableNoNagle | k_nSteamNetworkingSend_AutoRestartBrokenSession,channel)==k_EResultOK;
    }
    template<class Handler> void poll(Handler handler) {
        poll_bytes([&](std::uint64_t sender,std::span<const std::uint8_t> bytes) {
            const auto packet=decode(bytes);if(packet)handler(sender,*packet);
        });
    }
    template<class Handler> void poll_bytes(Handler handler) {
        if(!api_)return;
        SteamNetworkingMessage_t* messages[32]{};
        const auto count=api_->ReceiveMessagesOnChannel(channel,messages,32);
        for(int i=0;i<count && i<32;++i) {
            auto* m=messages[i];
            if(!m)continue;
            if(m->m_nChannel==channel && m->m_cbSize>0 && m->m_cbSize<=1024 && m->m_pData) {
                handler(m->m_identityPeer.GetSteamID64(),{static_cast<const std::uint8_t*>(m->m_pData),static_cast<std::size_t>(m->m_cbSize)});
            }
            m->Release();
        }
    }
    void shutdown() {
        if(callback_)callback_->disable();
        if(api_ && unregister_)unregister_(callback_);
        api_=nullptr;
    }
};
}
