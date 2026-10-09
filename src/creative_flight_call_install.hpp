#pragma once
#include <cstdint>
namespace xhl::creative_flight::call_install {
enum class Image { server, client };
enum class Status {
    published, unavailable, invalid_request, identity_failure, allocation_failure,
    thread_failure, unstable_threads, interior_ip, proof_failure, protect_failure,
    write_failure, verify_failure, cache_failure, rollback_failure, resume_failure,
    already_attempted, recovered
};
struct Result {
    Status status=Status::unavailable;
    bool published=false;
    // Publication attempted or uncertain: slot and pinned bridge remain alive.
    bool backing_retained=false;
    bool original_restored=false;
    bool site_complete=false;
    bool recovery_required=false; // Caller must preserve the frozen interval.
    unsigned attempts=0;
    unsigned pending_suspends=0; // Nonzero is a hard operational failure. Handles retained.
};
// Opt-in, once per image per process, serialized with every other code writer.
// Call outside DllMain/loader lock, with G effects disabled. bridge must be the
// matching already-verified real CALL/FRAME entry in a loaded native DLL.
// It hashes the complete main-module file and checks mapped dispatch evidence.
// Windows native AMD64 builds 19041..26100 and exact 26300; Wine unavailable.
// No uninstall: successful/uncertain publication permanently retains the slot
// and PINs the bridge DLL. Pinning is done before suspension and is permanent
// even if a later pre-publication check fails. Logical disable/drain belongs
// to the runtime. Persistent resume failure retains handles/bookkeeping and
// returns pending_suspends; do not attempt a different installer as fallback.
constexpr bool supported_windows_build(std::uint32_t build) noexcept {
    return (build>=19041 && build<=26100) || build==26300;
}
Result install(Image image, std::uintptr_t verified_bridge) noexcept;
// One bounded recovery attempt, on the ORIGINAL installer thread, outside
// loader lock. Uses retained preallocated state only. A failed recovery keeps
// the writer gate, backing, file and exact suspend ownership intact. Returning
// recovery_required means the caller must not allocate/log/load/close handles
// or create threads before another bounded recovery attempt.
Result recover_retained() noexcept;
}
