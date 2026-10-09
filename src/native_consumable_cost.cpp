#include "native_consumable_cost.hpp"

namespace xhl::native_consumable_cost {
namespace {
thread_local std::optional<ActionFrame> action_frame;

bool has_consumables_effect(const std::optional<std::uint16_t>& effects) noexcept {
    constexpr auto required = static_cast<std::uint16_t>(
        xhl::native_effects::Effect::free_consumables);
    return effects && !(*effects & ~xhl::native_effects::valid_effect_mask) &&
        ((*effects & required) != 0);
}

bool valid_identity(const xhl::flight::Identity& identity,
    const QueryOwner& query) noexcept {
    if (!query.world || query.owner < 1 || query.owner > 16 ||
        identity.world != query.world || identity.lifecycle == 0) return false;
    return identity.valid(query.owner - 1);
}

std::optional<xhl::flight::Identity> resolve_current(const Services& services,
    const QueryOwner& query) noexcept {
    if (!services.resolve_owner || !services.is_alive || !query.world ||
        query.owner < 1 || query.owner > 16) return {};

    const auto identity = services.resolve_owner(query.world, query.owner);
    if (!identity || !valid_identity(*identity, query) ||
        !services.is_alive(*identity)) return {};
    return identity;
}

bool matching_site(std::uintptr_t return_address, const ActionFrame& frame,
    std::uintptr_t& return_rva) noexcept {
    if (!frame.server_base || return_address < frame.server_base) return false;
    return_rva = return_address - frame.server_base;
    return (return_rva == used_removal_return_rva &&
            frame.kind == ActionKind::used) ||
        (return_rva == fired_removal_return_rva &&
            frame.kind == ActionKind::fired);
}
}

ActionScope::ActionScope(const ActionFrame& frame) noexcept
    : previous_(action_frame) {
    action_frame = frame;
}

ActionScope::~ActionScope() {
    action_frame = previous_;
}

std::optional<ActionFrame> current_action_frame() noexcept {
    return action_frame;
}

bool authorized_removal(std::uintptr_t native_return_address,
    const ActionFrame& frame, std::uint32_t removal_owner,
    std::uint64_t now_ms, const Services& services) noexcept {
    std::uintptr_t return_rva = 0;
    if (!matching_site(native_return_address, frame, return_rva) ||
        !frame.query_context || !removal_owner || !services.read_query_owner ||
        !services.resolve_owner || !services.is_alive || !services.active_effects)
        return false;

    QueryOwner before{};
    if (!services.read_query_owner(frame.query_context, before) ||
        !before.world || before.owner != removal_owner ||
        before.owner < 1 || before.owner > 16) return false;

    const auto first_identity = resolve_current(services, before);
    if (!first_identity ||
        !has_consumables_effect(services.active_effects(*first_identity, now_ms)))
        return false;

    QueryOwner after{};
    if (!services.read_query_owner(frame.query_context, after) ||
        after.world != before.world || after.owner != before.owner) return false;
    const auto second_identity = resolve_current(services, after);
    if (!second_identity || *second_identity != *first_identity ||
        !has_consumables_effect(services.active_effects(*second_identity, now_ms)))
        return false;

    return true;
}

bool invoke_native_removal(NativeRemoveItems original,
    const NativeRemovalArgs& args, std::uintptr_t native_return_address,
    std::uint64_t now_ms, const Services& services,
    void*& native_result) noexcept {
    native_result = nullptr;
    if (!original) return false;

    const auto frame = current_action_frame();
    if (args.result && frame && authorized_removal(native_return_address,
        *frame, args.owner, now_ms, services)) {
        args.result->status = 0;
        args.result->remainder = 0;
        native_result = args.result;
        return true;
    }

    native_result = original(args.result, args.transaction, args.owner,
        args.item, args.packed_slot_ref, args.quantity);
    return true;
}
}
