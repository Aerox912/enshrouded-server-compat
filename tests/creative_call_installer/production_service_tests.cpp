#include "creative_flight_call_service.hpp"
#include <Windows.h>
#include <iostream>
#include <stdexcept>
using namespace xhl::creative_flight::call_install;
int main(){try {
    auto& service=process_install_service();
    if(service.ready(Image::server)||service.safe_to_report())throw std::runtime_error("default gate opened");
    // This unpinned test EXE cannot pass the production full-file identity gate.
    if(!service.start({Image::server,1,true,true,true}))throw std::runtime_error("service start failed");
    const auto end=GetTickCount64()+5000;
    while(service.state()!=ServiceState::settled&&GetTickCount64()<end)Sleep(1);
    const auto result=service.snapshot();
    if(!service.safe_to_report()||(result.status!=Status::identity_failure&&result.status!=Status::unavailable)||
       result.published||result.recovery_required||result.pending_suspends||service.accept_runtime({true,true,true})||
       service.ready(Image::server)||service.start({Image::server,1,true,true,true}))throw std::runtime_error("production identity rejection escaped readiness gate");
    service.disable();if(!service.request_recovery())throw std::runtime_error("persistent owner event unavailable after safe rejection");
    std::cout<<"production service singleton/identity/rejection/lifetime checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
