#pragma once

#include <array>
#include <cstddef>

namespace xhl::native_cost_hooks {

// Injectable seam around MinHook so partial preparation and enable/disable
// failures can be exercised without installing a live detour.
struct Operations {
    bool (*create)(void* target, void* detour, void** original) noexcept = nullptr;
    bool (*remove)(void* target) noexcept = nullptr;
    bool (*enable)(void* target) noexcept = nullptr;
    bool (*disable)(void* target) noexcept = nullptr;
};

template<std::size_t Count>
class HookSet {
    struct Site {
        void* original = nullptr;
        bool prepared = false;
        bool enabled = false;
    };

    std::array<Site, Count> sites_{};

public:
    bool prepare(const std::array<void*, Count>& targets,
        const std::array<void*, Count>& detours,
        const Operations& operations) noexcept {
        if (!operations.create || !operations.remove) return false;

        std::array<bool, Count> created_this_attempt{};
        bool complete = true;
        for (std::size_t i = 0; i < Count; ++i) {
            if (sites_[i].prepared) continue;
            void* original = nullptr;
            if (!targets[i] || !detours[i] ||
                !operations.create(targets[i], detours[i], &original)) {
                complete = false;
                break;
            }
            sites_[i].original = original;
            sites_[i].prepared = true;
            created_this_attempt[i] = true;
            if (!original) {
                complete = false;
                break;
            }
        }

        if (complete) return true;

        // Keep any site whose removal fails prepared, including its trampoline.
        // A retry skips that site and only prepares missing hooks.
        for (std::size_t i = Count; i-- > 0;) {
            if (!created_this_attempt[i]) continue;
            if (operations.remove(targets[i])) {
                sites_[i].original = nullptr;
                sites_[i].prepared = false;
                sites_[i].enabled = false;
            }
        }
        return false;
    }

    bool enable_all(const std::array<void*, Count>& targets,
        const Operations& operations) noexcept {
        if (!operations.enable || !operations.disable || !all_prepared())
            return false;

        bool complete = true;
        for (std::size_t i = 0; i < Count; ++i) {
            if (sites_[i].enabled) continue;
            if (!targets[i] || !operations.enable(targets[i])) {
                complete = false;
                break;
            }
            sites_[i].enabled = true;
        }
        if (complete) return true;

        (void)disable_all(targets, operations);
        return false;
    }

    bool disable_all(const std::array<void*, Count>& targets,
        const Operations& operations) noexcept {
        if (!operations.disable) return false;
        bool complete = true;
        for (std::size_t i = Count; i-- > 0;) {
            if (!sites_[i].enabled) continue;
            if (targets[i] && operations.disable(targets[i]))
                sites_[i].enabled = false;
            else
                complete = false;
        }
        return complete;
    }

    bool all_prepared() const noexcept {
        for (const auto& site : sites_)
            if (!site.prepared || !site.original) return false;
        return true;
    }

    bool all_enabled() const noexcept {
        for (const auto& site : sites_) if (!site.enabled) return false;
        return true;
    }

    bool prepared(std::size_t index) const noexcept {
        return index < Count && sites_[index].prepared;
    }

    bool enabled(std::size_t index) const noexcept {
        return index < Count && sites_[index].enabled;
    }

    void* original(std::size_t index) const noexcept {
        return index < Count ? sites_[index].original : nullptr;
    }
};

} // namespace xhl::native_cost_hooks
