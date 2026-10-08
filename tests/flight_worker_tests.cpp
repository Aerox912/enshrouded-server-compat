#include <vector>
#include "flight_worker.hpp"
#include <flight_patch.h>
#include <iostream>
#include <stdexcept>

namespace {
int checks=0;
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
struct Input {
    std::atomic<bool> down{false}, fail{false};
    std::atomic<unsigned> changes{0},ticks{0},stops{0};
    Flight::Toggle toggle;
    HANDLE changed=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    HANDLE stopped=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    ~Input(){CloseHandle(changed);CloseHandle(stopped);}
    static void tick(void* value){
        auto& s=*static_cast<Input*>(value);
        ++s.ticks;
        if(s.fail.load())throw std::runtime_error("injected worker failure");
        if(s.toggle.sample(s.down.load(),true)){++s.changes;SetEvent(s.changed);}
    }
    static void finish(void* value){auto& s=*static_cast<Input*>(value);s.toggle={};++s.stops;SetEvent(s.stopped);}
};
}
int main(){try{
    Input input;xhl::flight::PollWorker worker;
    check(worker.start(Input::tick,Input::finish,&input),"start independent input worker");
    check(!worker.start(Input::tick,Input::finish,&input),"duplicate worker rejected");
    Sleep(40);
    // No loader Update call is made in this harness. A short press still reaches
    // the worker well before the loader's default 500 ms polling interval.
    input.down.store(true);
    const bool short_press=WaitForSingleObject(input.changed,200)==WAIT_OBJECT_0;
    input.down.store(false);
    check(short_press && input.changes.load()==1,"short input edge detected without loader update");
    Sleep(40);input.down.store(true);
    check(WaitForSingleObject(input.changed,200)==WAIT_OBJECT_0,"second edge toggles off promptly");
    Sleep(80);check(input.changes.load()==2,"held key does not toggle repeatedly");
    worker.stop();const auto ticks=input.ticks.load();Sleep(20);
    check(input.stops.load()==1 && input.ticks.load()==ticks,"stop joins worker and runs cleanup once");
    check(!worker.failed(),"normal lifecycle has no worker error");
    input.down.store(false);input.fail.store(true);ResetEvent(input.stopped);
    check(worker.start(Input::tick,Input::finish,&input),"worker can restart after deactivation");
    check(WaitForSingleObject(input.stopped,200)==WAIT_OBJECT_0 && worker.failed(),"worker error still executes cleanup");
    worker.stop();check(input.stops.load()==2,"cleanup not repeated when joining failed worker");
    std::cout<<checks<<" input-worker checks passed (simulated key state, real worker thread).\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
