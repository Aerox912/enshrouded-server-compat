#include "flight_hook.hpp"
#include <MinHook.h>
#include <atomic>
#include <cstring>

extern "C" {
void FlightPitchDetour();
void* FlightPitchContinue = nullptr;
extern const float FlightVanillaPitch = xhl::flight::vanilla_pitch;
extern const float FlightEnabledPitch = xhl::flight::enabled_pitch;
}
namespace {
std::atomic<xhl::flight::Authorizer> authorize{nullptr};
bool attempted = false;
std::atomic<std::uint64_t> probe_calls{0}, probe_allowed{0};
std::atomic<std::uintptr_t> probe_context{0};
std::atomic<std::uint32_t> probe_owner{0};
std::atomic<float> probe_pitch{0}, probe_velocity{0};
}
extern "C" bool FlightAuthorize(const void* context, std::uint32_t entity, float pitch, float velocity) noexcept {
    const auto callback = authorize.load(std::memory_order_acquire);
    const bool allowed = callback && context && entity >= 1 && entity <= 16 && callback(context, entity);
    probe_context.store(reinterpret_cast<std::uintptr_t>(context),std::memory_order_relaxed);
    probe_owner.store(entity,std::memory_order_relaxed);
    probe_pitch.store(pitch,std::memory_order_relaxed);probe_velocity.store(velocity,std::memory_order_relaxed);
    probe_calls.fetch_add(1,std::memory_order_relaxed);
    if(allowed)probe_allowed.fetch_add(1,std::memory_order_relaxed);
    return allowed;
}
xhl::flight::GlideProbe xhl::flight::glide_probe() noexcept {
    return {probe_calls.load(std::memory_order_relaxed),probe_allowed.load(std::memory_order_relaxed),
        probe_context.load(std::memory_order_relaxed),probe_owner.load(std::memory_order_relaxed),
        probe_pitch.load(std::memory_order_relaxed),probe_velocity.load(std::memory_order_relaxed)};
}
bool xhl::flight::install(HMODULE game, Authorizer callback, Logger logger) {
    const auto log = [logger](const char* text) { if (logger) logger(text); };
    if (attempted) return false;
    attempted = true;
    if (!callback || !validate_server(game)) {
        log("FLIGHT NOT APPLIED: missing authorizer or unsupported executable.");
        return false;
    }
    auto base = reinterpret_cast<unsigned char*>(game);
    constexpr unsigned char bytes[] = {
        0xf3,0x0f,0x10,0x05,0x63,0x20,0xaf,0x00,
        0xf2,0x0f,0x11,0x4c,0x24,0x40,0x77,0x15};
    // The original context and owner are still live in this pinned stack frame.
    constexpr unsigned char owner[] = {0x48,0x8d,0x95,0xb0,0x03,0,0};
    constexpr unsigned char frame[] = {0x48,0x89,0x4c,0x24,0x08,0x55,0x53,0x48,0x8d,0xac,0x24,0x38,0xec,0xff,0xff};
    if (std::memcmp(base + site, bytes, sizeof(bytes)) ||
        std::memcmp(base + 0xb567fc, &vanilla_pitch, sizeof(vanilla_pitch)) ||
        std::memcmp(base + 0x640c0, owner, sizeof(owner)) ||
        std::memcmp(base + 0x63f50, frame, sizeof(frame))) {
        log("FLIGHT NOT APPLIED: glide code or owner frame changed/already hooked.");
        return false;
    }
    const auto init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
        log("FLIGHT NOT APPLIED: hook engine unavailable.");
        return false;
    }
    void* original = nullptr;
    if (MH_CreateHook(base + site, reinterpret_cast<void*>(FlightPitchDetour), &original) != MH_OK) {
        log("FLIGHT NOT APPLIED: could not prepare glide hook.");
        return false;
    }
    // Replace exactly the first 8-byte MOVSS, retaining the following MOVSD and
    // JA with their original flags. No global constant or shared on/off bit.
    FlightPitchContinue = base + site + 8;
    authorize.store(callback, std::memory_order_release);
    if (MH_EnableHook(base + site) != MH_OK) {
        authorize.store(nullptr, std::memory_order_release);
        MH_DisableHook(base + site);
        // Keep the trampoline alive if another thread entered during failure.
        log("FLIGHT NOT APPLIED: activation failed; authorization disabled.");
        return false;
    }
    log("EXPERIMENTAL FLIGHT HOOK: per-call authorization; live identity/transport acceptance still required.");
    return true;
}
