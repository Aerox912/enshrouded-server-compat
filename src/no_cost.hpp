#pragma once
#include "native_effects.hpp"
#include <cstdint>
#include <span>

namespace xhl::no_cost {
inline constexpr std::size_t transaction_flags_offset = 0xb0;
inline constexpr std::size_t transaction_prefix_size = transaction_flags_offset + 1;
inline constexpr std::uint8_t crafting_ingredient_bypass = 0x08;

// The verified 0x1aba90 recipe-action wrapper is called as (event, recipe)
// and returns the native EAX status. Its body synchronously invokes the
// 0x150990 transaction processor with the ABI below.
using NativeCraftAction = std::uint32_t (__fastcall *)(void* event, void* recipe);

struct NativeCraftProcessorArgs {
    void* transaction = nullptr;       // RCX
    void* recipe = nullptr;            // RDX
    void* output = nullptr;            // R8
    std::uint64_t recipe_options = 0;  // R9
    void* validation_state = nullptr;  // stack argument 1
    void* action_data = nullptr;       // stack argument 2
    std::uint32_t operator_entity_id = 0; // stack argument 3: event row +0x08
    std::uint32_t invocation_flags = 0;// stack argument 4
};

using NativeCraftProcessor = std::uint32_t (__fastcall *)(
    void* transaction, void* recipe, void* output, std::uint64_t recipe_options,
    void* validation_state, void* action_data, std::uint32_t operator_entity_id,
    std::uint32_t invocation_flags);

// These adapters preserve every native argument and result. The action scope
// is thread-local and nested calls replace, then restore, the prior authority.
// A call with a missing original is reported to its hook without changing state.
bool invoke_native_crafting_action(NativeCraftAction original, void* event,
    void* recipe, const native_effects::Authorization* authorization,
    std::uint32_t& result);
bool invoke_native_crafting_processor(NativeCraftProcessor original,
    const NativeCraftProcessorArgs& args, std::uint32_t& result);

class CraftingActionScope {
    const native_effects::Authorization* previous_ = nullptr;
public:
    explicit CraftingActionScope(
        const native_effects::Authorization* authorization) noexcept;
    ~CraftingActionScope();

    CraftingActionScope(const CraftingActionScope&) = delete;
    CraftingActionScope& operator=(const CraftingActionScope&) = delete;
    CraftingActionScope(CraftingActionScope&&) = delete;
    CraftingActionScope& operator=(CraftingActionScope&&) = delete;
};

bool crafting_authorization_active() noexcept;
const native_effects::Authorization* current_crafting_authorization() noexcept;

// Scope this around the native transaction processor call only. It restores
// bit 0x08 on return so the native commit, rollback, and later transactions
// retain their ordinary behavior.
class CraftingTransactionScope {
    std::uint8_t* flags_ = nullptr;
    bool was_set_ = false;
public:
    CraftingTransactionScope(std::span<std::uint8_t> transaction,
        const native_effects::Authorization* authorization) noexcept;
    ~CraftingTransactionScope();

    CraftingTransactionScope(const CraftingTransactionScope&) = delete;
    CraftingTransactionScope& operator=(const CraftingTransactionScope&) = delete;
    CraftingTransactionScope(CraftingTransactionScope&&) = delete;
    CraftingTransactionScope& operator=(CraftingTransactionScope&&) = delete;

    bool active() const noexcept { return flags_ != nullptr; }
};
}
