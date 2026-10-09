#pragma once
#include "creative_flight_call_install.hpp"
#include <atomic>
namespace xhl::creative_flight::call_install {
// Assertions supplied only after the runtime owner has compared the exact
// pinned callback/query/state/Dive anchors and matched the real FRAME bridge.
// They never substitute for install()'s independent file/mapped/site proof.
struct EntryProof {
    Image image=Image::server;
    std::uintptr_t bridge=0;
    bool full_pinned_file=false,callback_bytes=false,matching_frame_bridge=false;
    bool valid() const noexcept {
        return (image==Image::server||image==Image::client)&&bridge&&
            full_pinned_file&&callback_bytes&&matching_frame_bridge;
    }
};
struct RuntimeAcceptance {
    bool original_routes=false,current_binding_provider=false,phase_order=false;
    bool valid() const noexcept {return original_routes&&current_binding_provider&&phase_order;}
};
enum class ServiceState : unsigned {idle,installing,recovering,parked,settled};
// Process-lifetime object, created before start(), outside DllMain/loader lock.
// No destructor, join, cancellation or uninstall. Its dedicated thread and
// code module stay alive even after logical disable or an installation failure.
// The runtime owner supplies no callbacks to this thread. While frozen it may
// only call the retained installer, lock-free atomics and checked kernel waits.
class InstallService {
public:
#if defined(CREATIVE_CALL_INSTALL_SELFTEST)
    InstallService() noexcept=default;
    ~InstallService()=default;
#endif
    InstallService(const InstallService&)=delete;
    InstallService& operator=(const InstallService&)=delete;
    bool start(EntryProof) noexcept;
    void disable() noexcept {accepted_.store(false,std::memory_order_release);}
    bool accept_runtime(RuntimeAcceptance proof) noexcept;
    bool ready(Image) const noexcept;
    ServiceState state() const noexcept {return state_.load(std::memory_order_acquire);}
    Result snapshot() const noexcept;
    // Logging, runtime stop/drain and other ordinary work is allowed only on a
    // settled safe result, never just because pending_suspends is zero.
    bool safe_to_report() const noexcept;
    bool request_recovery() noexcept; // Signals precreated event; never recovers on caller.
#if defined(CREATIVE_CALL_INSTALL_SELFTEST)
    using TestOperation=Result (*)(void*) noexcept;
    bool start_test(EntryProof,TestOperation,TestOperation,void*) noexcept;
#endif
private:
#if !defined(CREATIVE_CALL_INSTALL_SELFTEST)
    InstallService() noexcept=default;
    ~InstallService()=default;
#endif
    friend InstallService& process_install_service() noexcept;
    EntryProof entry_{};
    std::atomic<bool> started_{false},accepted_{false};
    std::atomic<ServiceState> state_{ServiceState::idle};
    std::atomic<std::uint64_t> result_{static_cast<unsigned>(Status::unavailable)};
    std::atomic<std::uintptr_t> event_{0},begin_event_{0};std::uintptr_t thread_=0;
#if defined(CREATIVE_CALL_INSTALL_SELFTEST)
    TestOperation test_install_=nullptr,test_recover_=nullptr;void* test_context_=nullptr;
#endif
    bool launch(EntryProof) noexcept;
    [[noreturn]] void owner_loop() noexcept;
    static unsigned long __stdcall worker(void*) noexcept;
    void store(Result) noexcept;
};
// Initialize this singleton before start() and never destroy its storage.
InstallService& process_install_service() noexcept;
static_assert(std::atomic<std::uintptr_t>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::atomic<ServiceState>::is_always_lock_free);
}
