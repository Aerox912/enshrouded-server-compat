#pragma once

#include "native_consumable_cost.hpp"

namespace xhl::native_consumable_runtime_adapter {

enum class Mode : std::uint8_t { inactive, read_only_probe, free_consumables };
enum class WriteOutcome : std::uint8_t { unchanged, committed, indeterminate };
enum class RemovalDisposition : std::uint8_t {
    forwarded,
    bypassed,
    indeterminate,
    no_original
};

struct RemovalOutcome {
    void* native_return = nullptr;
    RemovalDisposition disposition = RemovalDisposition::no_original;
};

using UsedAction = void (__fastcall *)(void* query_context);
using FiredAction = std::uint8_t (__fastcall *)(
    void* query_context, void* action_data);
using WriteAuthorizedResult = WriteOutcome (*)(
    native_consumable_cost::NativeRemovalResult*) noexcept;
using EffectModeCurrent = bool (*)() noexcept;

// These wrappers are the executable adapter seam used by the installed
// detours and by the failure/forwarding tests. They add only the action scopes;
// the native callbacks remain responsible for eligibility and outputs.
inline void invoke_used_action(UsedAction original, void* query_context,
    std::uintptr_t server_base, Mode mode);
inline std::uint8_t invoke_fired_action(FiredAction original, void* query_context,
    void* action_data, std::uintptr_t server_base, Mode mode);

// A result write is attempted only after exact return-PC/action/owner/life/
// lease authorization and a final activation check. Any missing service,
// scope, result pointer, or write permission takes the original six-argument
// native path exactly once.
inline RemovalOutcome invoke_removal(
    native_consumable_cost::NativeRemoveItems original,
    const native_consumable_cost::NativeRemovalArgs& args,
    std::uintptr_t native_return_address, std::uint64_t now_ms, Mode mode,
    const native_consumable_cost::Services& services,
    WriteAuthorizedResult write_result,
    EffectModeCurrent effect_mode_current) noexcept;

inline void invoke_used_action(UsedAction original, void* query_context,
    std::uintptr_t server_base, Mode mode) {
    if (!original) return;
    if (mode != Mode::free_consumables) {
        original(query_context);
        return;
    }
    const native_consumable_cost::ActionFrame frame{
        native_consumable_cost::ActionKind::used, server_base,
        reinterpret_cast<std::uintptr_t>(query_context)};
    native_consumable_cost::ActionScope scope(frame);
    original(query_context);
}

inline std::uint8_t invoke_fired_action(FiredAction original,
    void* query_context, void* action_data, std::uintptr_t server_base,
    Mode mode) {
    if (!original) return 0;
    if (mode == Mode::inactive) return original(query_context, action_data);
    const native_consumable_cost::ActionFrame frame{
        native_consumable_cost::ActionKind::fired, server_base,
        reinterpret_cast<std::uintptr_t>(query_context)};
    native_consumable_cost::ActionScope scope(frame);
    return original(query_context, action_data);
}

inline RemovalOutcome invoke_removal(
    native_consumable_cost::NativeRemoveItems original,
    const native_consumable_cost::NativeRemovalArgs& args,
    std::uintptr_t native_return_address, std::uint64_t now_ms, Mode mode,
    const native_consumable_cost::Services& services,
    WriteAuthorizedResult write_result,
    EffectModeCurrent effect_mode_current) noexcept {
    using namespace native_consumable_cost;
    if (!original) return {nullptr, RemovalDisposition::no_original};

    const auto call_original = [&]() noexcept {
        return RemovalOutcome{original(args.result, args.transaction,
            args.owner, args.item, args.packed_slot_ref, args.quantity),
            RemovalDisposition::forwarded};
    };
    if (mode != Mode::free_consumables || !args.result || !write_result ||
        !effect_mode_current) return call_original();

    const auto frame = current_action_frame();
    if (!frame || !authorized_removal(native_return_address, *frame,
        args.owner, now_ms, services) || !effect_mode_current())
        return call_original();
    const auto write = write_result(args.result);
    if (write == WriteOutcome::unchanged) return call_original();
    if (write == WriteOutcome::indeterminate) {
        // Do not claim success or call payment again after an ambiguous write.
        return {nullptr, RemovalDisposition::indeterminate};
    }
    return {args.result, RemovalDisposition::bypassed};
}

} // namespace xhl::native_consumable_runtime_adapter
