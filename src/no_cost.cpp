#include "no_cost.hpp"

namespace xhl::no_cost {
namespace {
thread_local const native_effects::Authorization* current_authorization_value = nullptr;
}

CraftingActionScope::CraftingActionScope(
    const native_effects::Authorization* authorization) noexcept
    : previous_(current_authorization_value) {
    // A nested event without free-crafting authority must mask an enclosing
    // action, then restore it on return.
    current_authorization_value = authorization &&
        authorization->effect() == native_effects::Effect::free_crafting
        ? authorization : nullptr;
}

CraftingActionScope::~CraftingActionScope() {
    current_authorization_value = previous_;
}

bool crafting_authorization_active() noexcept {
    return current_authorization_value && current_authorization_value->effect() ==
        native_effects::Effect::free_crafting;
}

const native_effects::Authorization* current_crafting_authorization() noexcept {
    return crafting_authorization_active() ? current_authorization_value : nullptr;
}

bool invoke_native_crafting_action(NativeCraftAction original, void* event,
    void* recipe, const native_effects::Authorization* authorization,
    std::uint32_t& result) {
    if (!original) return false;
    CraftingActionScope scope(authorization);
    result = original(event, recipe);
    return true;
}

bool invoke_native_crafting_processor(NativeCraftProcessor original,
    const NativeCraftProcessorArgs& args, std::uint32_t& result) {
    if (!original) return false;

    std::span<std::uint8_t> transaction;
    if (args.transaction) {
        transaction = std::span<std::uint8_t>(
            static_cast<std::uint8_t*>(args.transaction), transaction_prefix_size);
    }
    CraftingTransactionScope bypass(transaction, current_crafting_authorization());
    result = original(args.transaction, args.recipe, args.output,
        args.recipe_options, args.validation_state, args.action_data,
        args.operator_entity_id, args.invocation_flags);
    return true;
}

CraftingTransactionScope::CraftingTransactionScope(
    std::span<std::uint8_t> transaction,
    const native_effects::Authorization* authorization) noexcept {
    if (!authorization || authorization->effect() != native_effects::Effect::free_crafting ||
        transaction.size() <= transaction_flags_offset) return;

    flags_ = &transaction[transaction_flags_offset];
    was_set_ = (*flags_ & crafting_ingredient_bypass) != 0;
    *flags_ = static_cast<std::uint8_t>(*flags_ | crafting_ingredient_bypass);
}

CraftingTransactionScope::~CraftingTransactionScope() {
    if (!flags_) return;
    if (was_set_) *flags_ = static_cast<std::uint8_t>(*flags_ | crafting_ingredient_bypass);
    else *flags_ = static_cast<std::uint8_t>(*flags_ & ~crafting_ingredient_bypass);
}
}
