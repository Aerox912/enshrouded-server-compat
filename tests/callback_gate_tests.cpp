#include "callback_gate.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>
using namespace std::chrono_literals;
int main() {
    xhl::flight::CallbackGate<int*> gate;
    int accepted=0;
    gate.configure(&accepted);
    std::promise<void> entered,release,disabling;
    auto resume=release.get_future();
    std::thread callback([&] {gate.invoke([&](int* state) {entered.set_value();resume.wait();if(state)++*state;});});
    entered.get_future().wait();
    auto stop=std::async(std::launch::async,[&] {disabling.set_value();gate.disable();});
    disabling.get_future().wait();
    const bool drained=stop.wait_for(30ms)==std::future_status::timeout;
    release.set_value();callback.join();stop.get();
    gate.invoke([](int* state) {if(state)++*state;}); // Late dispatch after unregister.
    if(!drained || accepted!=1){std::cerr<<"callback shutdown failed\n";return 1;}
    int next=0;
    gate.configure(&next);gate.invoke([](int* state){if(state)++*state;});gate.disable();
    gate.invoke([](int* state){if(state)++*state;});
    if(next!=1 || accepted!=1){std::cerr<<"callback reactivation failed\n";return 1;}
    xhl::flight::CallbackGate<int*> replacement;
    replacement.configure(&next);
    gate.invoke([](int* state){if(state)++*state;}); // Retired registration enters late.
    replacement.invoke([](int* state){if(state)++*state;});
    replacement.disable();
    if(next!=2 || accepted!=1){std::cerr<<"retired registration crossed activation\n";return 1;}
    std::cout<<"5 callback drain, late-dispatch and registration-isolation checks passed\n";
}
