#pragma once

#include "native_effects.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <windows.h>

namespace xhl::native_consumable_diagnostic {

inline constexpr std::size_t observation_capacity = 64;
inline constexpr bool enabled_by_default = false;

struct FiredObservation {
    std::uint64_t sequence = 0;
    std::uintptr_t caller_return_rva = 0;
    std::uintptr_t world = 0;
    // The pinned callsite loads the query-entity result directly into the
    // native remover's R8D owner argument. Both values are recorded without
    // re-running the native query helper or disturbing its cursor.
    std::uint32_t query_owner = 0;
    std::uint32_t removal_owner = 0;
    // Raw observed return IP inside the known enclosing native function range.
    // Its system registration is not proven and is never reported as such.
    std::uintptr_t enclosing_frame_rva = 0;
    std::uintptr_t registered_callback_rva = 0;
    bool query_owner_forwarding_proven = false;
    bool owner_identity_current = false;
    bool actor_alive = false;
    bool lease_known = false;
    bool free_consumables_lease_active = false;
    bool callback_registration_proven = false;
};

// Fixed-size, nonblocking record storage for a read-only probe. If a slot is
// busy, a record is dropped rather than delaying the server callback.
class FiredObservationRing {
    struct Slot {
        mutable std::atomic_flag lock = ATOMIC_FLAG_INIT;
        FiredObservation value{};
    };
    std::array<Slot, observation_capacity> slots_{};
    std::atomic<std::uint64_t> next_sequence_{1};
    std::atomic<std::uint64_t> dropped_{0};
public:
    void append(FiredObservation observation) noexcept {
        const auto sequence = next_sequence_.fetch_add(1,
            std::memory_order_relaxed);
        observation.sequence = sequence;
        auto& slot = slots_[(sequence - 1) % slots_.size()];
        if (slot.lock.test_and_set(std::memory_order_acquire)) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (slot.value.sequence <= sequence) slot.value = observation;
        else dropped_.fetch_add(1, std::memory_order_relaxed);
        slot.lock.clear(std::memory_order_release);
    }
    std::size_t read_after(std::uint64_t sequence,
        FiredObservation* output, std::size_t capacity) noexcept {
        if (!output || !capacity) return 0;
        std::array<FiredObservation, observation_capacity> snapshot{};
        std::size_t count = 0;
        for (auto& slot : slots_) {
            if (slot.lock.test_and_set(std::memory_order_acquire)) continue;
            if (slot.value.sequence) snapshot[count++] = slot.value;
            slot.lock.clear(std::memory_order_release);
        }
        std::sort(snapshot.begin(), snapshot.begin() + count,
            [](const auto& left, const auto& right) {
                return left.sequence < right.sequence;
            });
        std::size_t copied = 0;
        for (std::size_t i = 0; i < count && copied < capacity; ++i) {
            if (snapshot[i].sequence > sequence) output[copied++] = snapshot[i];
        }
        return copied;
    }
    std::uint64_t dropped_records() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }
};

// Preparation validates the pinned image and creates the shared used-action,
// fired-helper, and inventory-removal detours without enabling them. The probe
// remains inactive until enable_read_only_probe is explicitly called. The
// optional lease callback must be a nonrecursive, read-only snapshot. Once
// preparation begins, its function and all state it reads must remain valid
// for the process lifetime because retained detours and in-flight calls can
// outlive disable or failed removal. Diagnostic mode does not change or
// advertise an effect, and the fired outer-system registration remains
// unresolved.
bool prepare_read_only_probe(HMODULE server,
    native_effects::Services::ActiveEffects current_effects = nullptr) noexcept;
bool enable_read_only_probe() noexcept;
bool disable_read_only_probe() noexcept;
bool read_only_probe_enabled() noexcept;

// The used actor_apply_buff callback, fired helper, and shared remover are
// owned by this same hook set. These calls prepare first and explicitly enable
// only the free-consumables mode; no hook or effect is active by default.
bool prepare_free_consumables_hooks(HMODULE server,
    native_effects::Services::ActiveEffects current_effects) noexcept;
bool enable_free_consumables() noexcept;
bool disable_free_consumables() noexcept;
bool free_consumables_active() noexcept;
std::size_t read_observations_after(std::uint64_t sequence,
    FiredObservation* output, std::size_t capacity) noexcept;
std::uint64_t dropped_observations() noexcept;

} // namespace xhl::native_consumable_diagnostic
