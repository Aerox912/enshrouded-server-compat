#include "creative_flight_call_service.hpp"
#include <Windows.h>
#include <intrin.h>
namespace xhl::creative_flight::call_install {
namespace {
std::uint64_t encode(Result r) noexcept {
    return static_cast<unsigned>(r.status) | (std::uint64_t(r.published)<<8) |
        (std::uint64_t(r.backing_retained)<<9) | (std::uint64_t(r.original_restored)<<10) |
        (std::uint64_t(r.site_complete)<<11) | (std::uint64_t(r.recovery_required)<<12) |
        (std::uint64_t(r.attempts&255)<<16) | (std::uint64_t(r.pending_suspends)<<32);
}
Result decode(std::uint64_t v) noexcept {
    Result r;r.status=static_cast<Status>(v&255);r.published=(v&(1ull<<8))!=0;
    r.backing_retained=(v&(1ull<<9))!=0;r.original_restored=(v&(1ull<<10))!=0;
    r.site_complete=(v&(1ull<<11))!=0;r.recovery_required=(v&(1ull<<12))!=0;
    r.attempts=static_cast<unsigned>((v>>16)&255);r.pending_suspends=static_cast<unsigned>(v>>32);return r;
}
}
InstallService& process_install_service() noexcept {static InstallService service;return service;}
void InstallService::store(Result r) noexcept {result_.store(encode(r),std::memory_order_release);}
Result InstallService::snapshot() const noexcept {return decode(result_.load(std::memory_order_acquire));}
bool InstallService::safe_to_report() const noexcept {
    return state()==ServiceState::settled&&!snapshot().recovery_required;
}
bool InstallService::ready(Image image) const noexcept {
    if(state()!=ServiceState::settled||!accepted_.load(std::memory_order_acquire))return false;
    const auto r=snapshot();return entry_.image==image&&r.published&&r.site_complete&&
        !r.recovery_required&&!r.pending_suspends;
}
bool InstallService::accept_runtime(RuntimeAcceptance proof) noexcept {
    const auto r=snapshot();const bool valid=proof.valid()&&safe_to_report()&&r.published&&r.site_complete&&!r.pending_suspends;
    accepted_.store(valid,std::memory_order_release);return valid;
}
bool InstallService::start(EntryProof entry) noexcept {return launch(entry);}
bool InstallService::launch(EntryProof entry) noexcept {
    if(!entry.valid())return false;
    bool expected=false;if(!started_.compare_exchange_strong(expected,true,std::memory_order_acq_rel))return false;
    // PIN our code before creating any thread. The bridge's separate DLL and
    // unwind checks remain inside install(); all service data must live forever.
    HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&InstallService::worker),&module)){store({Status::allocation_failure});
        state_.store(ServiceState::settled,std::memory_order_release);return false;}
    const auto event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event){store({Status::allocation_failure});
        state_.store(ServiceState::settled,std::memory_order_release);return false;}
    entry_=entry;event_.store(reinterpret_cast<std::uintptr_t>(event),std::memory_order_release);
    const auto begin=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!begin){const bool closed=CloseHandle(event)!=FALSE;if(closed)event_.store(0,std::memory_order_release);
        store({closed?Status::allocation_failure:Status::thread_failure});
        state_.store(ServiceState::settled,std::memory_order_release);return false;}
    begin_event_.store(reinterpret_cast<std::uintptr_t>(begin),std::memory_order_release);
    state_.store(ServiceState::installing,std::memory_order_release);
    const auto thread=CreateThread(nullptr,0,&InstallService::worker,this,0,nullptr);
    if(!thread){const bool closed=CloseHandle(event)!=FALSE;if(closed)event_.store(0,std::memory_order_release);
        const bool begin_closed=CloseHandle(begin)!=FALSE;if(begin_closed)begin_event_.store(0,std::memory_order_release);
        store({closed&&begin_closed?Status::allocation_failure:Status::thread_failure});
        state_.store(ServiceState::settled,std::memory_order_release);return false;}
    // Record EVERY handle before allowing the worker to freeze its creator.
    thread_=reinterpret_cast<std::uintptr_t>(thread);
    if(!SetEvent(begin)){store({Status::thread_failure});
        state_.store(ServiceState::settled,std::memory_order_release);return false;}
    return true;
}
bool InstallService::request_recovery() noexcept {
    const auto event=event_.load(std::memory_order_acquire);
    return event&&SetEvent(reinterpret_cast<HANDLE>(event))!=FALSE;
}
unsigned long __stdcall InstallService::worker(void* context) noexcept {
    static_cast<InstallService*>(context)->owner_loop();
}
void InstallService::owner_loop() noexcept {
    if(WaitForSingleObject(reinterpret_cast<HANDLE>(begin_event_.load(std::memory_order_acquire)),INFINITE)!=WAIT_OBJECT_0)__fastfail(7);
    Result r;
#if defined(CREATIVE_CALL_INSTALL_SELFTEST)
    if(test_install_)r=test_install_(test_context_);else
#endif
    r=install(entry_.image,entry_.bridge);
    store(r);
    for(;;){
        if(r.recovery_required){
            state_.store(ServiceState::recovering,std::memory_order_release);
            // A batch is bounded. Never exit or invoke ordinary runtime/error
            // callbacks while ownership is retained, including zero-peer holds.
            for(unsigned attempt=0;attempt<3&&r.recovery_required;++attempt){
#if defined(CREATIVE_CALL_INSTALL_SELFTEST)
                if(test_recover_)r=test_recover_(test_context_);else
#endif
                r=recover_retained();
                store(r);
            }
        }
        state_.store(r.recovery_required?ServiceState::parked:ServiceState::settled,std::memory_order_release);
        // Automatic later batches use a checked kernel wait, not a busy spin.
        // The same persistent thread retains all recovery ownership throughout.
        const auto waited=WaitForSingleObject(reinterpret_cast<HANDLE>(event_.load(std::memory_order_acquire)),r.recovery_required?1000:INFINITE);
        if(waited!=WAIT_OBJECT_0&&waited!=WAIT_TIMEOUT){
            disable();__fastfail(7); // Corrupt wait ownership: terminate; never thaw/return.
        }
    }
}
#if defined(CREATIVE_CALL_INSTALL_SELFTEST)
bool InstallService::start_test(EntryProof entry,TestOperation first,TestOperation next,void* context) noexcept {
    if(started_.load(std::memory_order_acquire)||!first||!next)return false;
    test_install_=first;test_recover_=next;test_context_=context;return launch(entry);
}
#endif
}
