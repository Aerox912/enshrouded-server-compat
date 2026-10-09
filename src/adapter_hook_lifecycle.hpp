#pragma once

#include <array>
#include <cstddef>

namespace xhl::adapter_hooks {

// The operations express whether the requested target state was achieved.
// MinHook's MH_ERROR_ENABLED and MH_ERROR_DISABLED are successful desired
// states and should be normalized to true by the production adapter.
struct Operations {
    void* context = nullptr;
    bool (*create)(void* context, void* target, void* detour,
        void** original) noexcept = nullptr;
    bool (*remove)(void* context, void* target) noexcept = nullptr;
    bool (*enable)(void* context, void* target) noexcept = nullptr;
    bool (*disable)(void* context, void* target) noexcept = nullptr;
};

template<std::size_t Count>
class HookSet {
    struct Site {
        void* original = nullptr;
        bool prepared = false;
        bool enabled = false;
        bool ever_enabled = false;
    };

    std::array<Site, Count> sites_{};

    static void clear(Site& site) noexcept {
        site.original = nullptr;
        site.prepared = false;
        site.enabled = false;
        // ever_enabled is deliberately not reset. An activated trampoline is
        // never eligible for removal, even after a successful disable.
    }

public:
    bool prepare(const std::array<void*, Count>& target_sites,
        const std::array<void*, Count>& detour_sites,
        const Operations& operations) noexcept {
        if (!operations.create || !operations.remove) return false;

        std::array<bool, Count> created_this_attempt{};
        bool complete = true;

        for (std::size_t i = 0; i < Count; ++i) {
            auto& site = sites_[i];
            if (site.prepared && site.original) continue;

            // A successful create without a usable trampoline is retained
            // until its own removal succeeds. Do not overwrite that hook.
            if (site.prepared) {
                if (site.ever_enabled || !target_sites[i] ||
                    !operations.remove(operations.context, target_sites[i])) {
                    complete = false;
                    break;
                }
                clear(site);
            }

            void* original = nullptr;
            if (!target_sites[i] || !detour_sites[i] ||
                !operations.create(operations.context, target_sites[i], detour_sites[i],
                    &original)) {
                complete = false;
                break;
            }

            site.original = original;
            site.prepared = true;
            created_this_attempt[i] = true;
            if (!original) {
                complete = false;
                break;
            }
        }

        if (complete) return true;

        // Roll back only hooks created by this attempt and never enabled.
        // Failed removals remain owned, prepared, and retryable.
        for (std::size_t i = Count; i-- > 0;) {
            auto& site = sites_[i];
            if (!created_this_attempt[i] || site.ever_enabled) continue;
            if (target_sites[i] &&
                operations.remove(operations.context, target_sites[i])) {
                clear(site);
            }
        }
        return false;
    }

    // Enables each owned target directly. There is intentionally no queued
    // operation here: applying MinHook's process-wide queue could also apply
    // another module's pending state.
    bool enable_all(const std::array<void*, Count>& target_sites,
        const Operations& operations) noexcept {
        if (!operations.enable || !all_prepared()) return false;

        for (std::size_t i = 0; i < Count; ++i) {
            auto& site = sites_[i];
            if (site.enabled) continue;
            if (!target_sites[i] ||
                !operations.enable(operations.context, target_sites[i])) {
                return false;
            }
            site.enabled = true;
            site.ever_enabled = true;
        }
        return true;
    }

    // Disables only owned sites that this set believes may be enabled. A
    // failed disable remains enabled in state so callers can report uncertainty
    // and a later attempt can retry. No activated trampoline is removed here.
    bool disable_enabled(const std::array<void*, Count>& target_sites,
        const Operations& operations) noexcept {
        if (!operations.disable) return false;

        bool complete = true;
        for (std::size_t i = Count; i-- > 0;) {
            auto& site = sites_[i];
            if (!site.enabled) continue;
            if (target_sites[i] &&
                operations.disable(operations.context, target_sites[i])) {
                site.enabled = false;
            } else {
                complete = false;
            }
        }
        return complete;
    }

    bool all_prepared() const noexcept {
        for (const auto& site : sites_)
            if (!site.prepared || !site.original) return false;
        return true;
    }

    bool all_enabled() const noexcept {
        for (const auto& site : sites_)
            if (!site.enabled) return false;
        return true;
    }

    bool any_prepared() const noexcept {
        for (const auto& site : sites_) if (site.prepared) return true;
        return false;
    }
    bool any_enabled() const noexcept {
        for (const auto& site : sites_)
            if (site.enabled) return true;
        return false;
    }

    bool prepared(std::size_t index) const noexcept {
        return index < Count && sites_[index].prepared;
    }

    bool enabled(std::size_t index) const noexcept {
        return index < Count && sites_[index].enabled;
    }

    bool ever_enabled(std::size_t index) const noexcept {
        return index < Count && sites_[index].ever_enabled;
    }

    void* original(std::size_t index) const noexcept {
        return index < Count ? sites_[index].original : nullptr;
    }
};

} // namespace xhl::adapter_hooks