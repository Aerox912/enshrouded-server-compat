#pragma once

#include "native_effects.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace xhl::native_consumable_cost {
inline constexpr std::uintptr_t used_removal_return_rva = 0x5c985;
inline constexpr std::uintptr_t fired_removal_return_rva = 0x30ce9;

enum class ActionKind : std::uint8_t { used, fired };

struct ActionFrame {
    ActionKind kind = ActionKind::used;
    std::uintptr_t server_base = 0;
    std::uintptr_t query_context = 0;
};

// Scope this around one native action/query wrapper. The query context remains
// stack-owned by the server callback and is only read synchronously from the
// nested removal hook. Nested scopes replace and restore the prior frame.
class ActionScope {
    std::optional<ActionFrame> previous_{};
public:
    explicit ActionScope(const ActionFrame&) noexcept;
    ~ActionScope();

    ActionScope(const ActionScope&) = delete;
    ActionScope& operator=(const ActionScope&) = delete;
    ActionScope(ActionScope&&) = delete;
    ActionScope& operator=(ActionScope&&) = delete;
};

std::optional<ActionFrame> current_action_frame() noexcept;

struct QueryOwner {
    std::uintptr_t world = 0;
    // Native query entity returned by 0x5d5130. For these caller sites the
    // native code passes this same value as inventory.removeItems owner.
    std::uint32_t owner = 0;
};

struct Services {
    using ReadQueryOwner = bool (*)(std::uintptr_t query_context,
        QueryOwner& out) noexcept;
    using ResolveOwner = std::optional<xhl::flight::Identity> (*)(
        std::uintptr_t world, std::uint32_t owner) noexcept;
    using IsAlive = bool (*)(const xhl::flight::Identity&) noexcept;
    // Must be a nonrecursive snapshot of the current owner-bound Creative
    // effect lease. It must not acquire a core guard already held by the caller.
    using ActiveEffects = std::optional<std::uint16_t> (*)(
        const xhl::flight::Identity&, std::uint64_t now_ms) noexcept;

    ReadQueryOwner read_query_owner = nullptr;
    ResolveOwner resolve_owner = nullptr;
    IsAlive is_alive = nullptr;
    ActiveEffects active_effects = nullptr;
};

// The result fields match the native caller's checks: byte status at +0 and
// remaining amount at +4. Authorized consumables set only these two fields.
struct NativeRemovalResult {
    std::uint8_t status = 0;
    std::uint8_t padding[3]{};
    std::uint32_t remainder = 0;
};
static_assert(sizeof(NativeRemovalResult) == 8);
static_assert(offsetof(NativeRemovalResult, remainder) == 4);

struct NativeRemovalArgs {
    NativeRemovalResult* result = nullptr; // RCX
    void* transaction = nullptr;           // RDX
    std::uint32_t owner = 0;               // R8D
    std::uint32_t item = 0;                // R9D
    std::uint64_t packed_slot_ref = 0;     // stack argument 5
    std::uint32_t quantity = 0;            // stack argument 6
};

using NativeRemoveItems = void* (__fastcall *)(void* result, void* transaction,
    std::uint32_t owner, std::uint32_t item, std::uint64_t packed_slot_ref,
    std::uint32_t quantity);

// Check the actual native return PC, scoped action kind, live query owner,
// current authenticated Identity, actor liveness, and current free-consumables
// lease. Unknown, stale, mismatched, or ordinary calls fail closed.
bool authorized_removal(std::uintptr_t native_return_address,
    const ActionFrame&, std::uint32_t removal_owner, std::uint64_t now_ms,
    const Services&) noexcept;

// Integration seam for a future exact-site removeItems detour. Unauthorized
// calls invoke the original with every native argument unchanged. An authorized
// call skips only the native payment removal, writes the two verified result
// fields, and returns the native result pointer.
bool invoke_native_removal(NativeRemoveItems original,
    const NativeRemovalArgs&, std::uintptr_t native_return_address,
    std::uint64_t now_ms, const Services&, void*& native_result) noexcept;
}
