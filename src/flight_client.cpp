#include "flight_client_host.hpp"
#include <vector>
#include <shroudtopia.h>
#include <notice.h>
#include "adapter.hpp"
#include "flight_identity.hpp"
#include "flight_transport.hpp"
#include "flight_worker.hpp"
#include <MinHook.h>
#include <bcrypt.h>
#include <mutex>

namespace {
using namespace xhl::flight;
using namespace xhl::client;
bool prepared=false;
class FlightMod final : public Mod {
    ClientSession session;
    SteamTransport transport;
    Toggle toggle;
    FlightNotice notice;
    Host connected;
    std::uint64_t sent=0,last_reply=0;
    int key=VK_F6;
    bool visible_on=false;
    std::atomic<int> configured_key{VK_F6};
    PollWorker worker;
    std::mutex logs_guard;
    std::vector<std::string> logs;
    bool failure_reported=false;
    void log(std::string message) {
        std::lock_guard lock(logs_guard);
        if(logs.size()<64)logs.push_back("[Flight] t="+std::to_string(GetTickCount64())+" "+message);
    }
    void drain(ModContext* c) {
        std::vector<std::string> pending;
        {std::lock_guard lock(logs_guard);pending.swap(logs);}
        for(const auto& message:pending)c->Log(message.c_str());
    }
    void finish() {
        set_local_flight(false);
        if(connected.steam){auto off=session.request(false,GetTickCount64());if(off)transport.send(connected.steam,*off);}
        transport.shutdown();session.reset();toggle={};connected={};notice.release();visible_on=false;
    }
public:
    ~FlightMod() override {worker.stop();}
    ModMetaData GetMetaData() override {return {"Flight Mod","Server-approved glider flight (development build).","2.0.0-dev","s0T7x; maintained by Aerox912","0.1.1",true,false};}
    void Load(ModContext* c) override {
        if(!c->game.isClient)return;
        static bool attempted=false;if(!attempted){attempted=true;prepared=prepare();}
        c->Log(prepared?"[Flight] development client loaded; waiting for authenticated server approval":"[Flight] unsupported build or hook conflict; vanilla flight retained");
    }
    void Activate(ModContext* c) override {
        if(active || !prepared)return;
        configured_key.store(c->config.GetInt?c->config.GetInt("Flight Mod","toggle_key",VK_F6):VK_F6);
        session.reset();toggle={};set_local_flight(false);failure_reported=false;
        active=worker.start([](void* self){static_cast<FlightMod*>(self)->tick();},
            [](void* self){static_cast<FlightMod*>(self)->finish();},this);
        c->Log(active?"[Flight] input/network worker started (8 ms polling)":"[Flight] input worker failed; vanilla flight retained");
    }
    void Deactivate(ModContext* c) override {worker.stop();set_local_flight(false);active=false;drain(c);}
    void Unload(ModContext* c) override {Deactivate(c);loaded=false;}
    void Update(ModContext* c) override {
        if(!active)return;
        configured_key.store(c->config.GetInt?c->config.GetInt("Flight Mod","toggle_key",VK_F6):VK_F6);
        drain(c);
        if(worker.failed() && !failure_reported){failure_reported=true;c->Log("[Flight] input worker stopped after an error; vanilla flight retained");}
    }
private:
    void tick() {
        const auto now=GetTickCount64();const auto next=current_host();
        if(next.generation!=connected.generation || next.steam!=connected.steam){
            session.reset(next.steam?random_number():0);toggle={};set_local_flight(false);connected=next;sent=last_reply=0;
            log(next.steam?"game server discovered; requesting approval":"game connection cleared");
        }
        if(connected.steam && transport.initialize(false,allowed_host)) {
            transport.poll([&](std::uint64_t sender,const Message& response){
                const bool was_ready=session.ready();
                if(sender==connected.steam && current_host().generation==connected.generation && session.receive(response,now)) {
                    last_reply=now;
                    if(!was_ready && session.ready())log("server challenge received; F6 available");
                }
            });
            if(last_reply && now-last_reply>=lease_ms){session.reset(random_number());toggle={};last_reply=0;sent=0;}
            if(!sent || now-sent>=1000){
                if(session.ready()){const auto request=session.request(toggle.on,now);if(request)transport.send(connected.steam,*request);}
                else transport.send(connected.steam,session.hello());
                sent=now;
            }
        }
        const auto configured=configured_key.load();
        if(configured!=key){key=configured;toggle.armed=false;}
        const auto window=GetForegroundWindow();DWORD pid=0;GetWindowThreadProcessId(window,&pid);
        CURSORINFO cursor{sizeof(CURSORINFO)};
        const bool focused=pid==GetCurrentProcessId() && (!GetCursorInfo(&cursor)||(cursor.flags&CURSOR_SHOWING)==0);
        const bool down=key>0 && key<255 && (GetAsyncKeyState(key)&0x8000)!=0;
        if(toggle.sample(down,focused)) {
            log(toggle.on?"key pressed: request on":"key pressed: request off");
            if(!session.ready()){toggle.on=false;log("unavailable: server companion and SteamID approval required");}
            else {const auto request=session.request(toggle.on,now);if(request)transport.send(connected.steam,*request);sent=now;}
        }
        const bool enabled=connected.steam && session.can_fly(now);
        set_local_flight(enabled);
        if(enabled!=visible_on){notice.show(enabled);log(enabled?"server-confirmed on":"off or approval expired");visible_on=enabled;}
        notice.update(focused?window:nullptr);
    }
};
}
extern "C" __declspec(dllexport) Mod* CreateModInstance(){return new FlightMod();}
BOOL APIENTRY DllMain(HMODULE,DWORD,LPVOID){return TRUE;}
