#include "flight_identity.hpp"

namespace xhl::flight {
AuthProofs::Pending AuthProofs::begin(const Peer& p) noexcept {
    if (!p.backend || !p.handle || (p.handle & 63) >= 16 || !p.steam ||
        !p.auth_enabled || !p.hosting || p.state != 1) return {};
    if (backend_ != p.backend) { proofs_ = {}; backend_ = p.backend; ++generation_; }
    const auto& proof = proofs_[p.handle & 63];
    return {p, generation_, proof.epoch};
}
bool AuthProofs::finish(const Pending& ticket, const Peer& after, bool success) noexcept {
    if (!success) { remove(after.backend,after.handle); return false; }
    if (!ticket.generation || ticket.generation != generation_ || backend_ != after.backend ||
        !ticket.peer.same_connection(after) || !after.auth_enabled || !after.hosting || after.state != 2) return false;
    auto& proof = proofs_[after.handle & 63];
    if (proof.epoch != ticket.epoch || serial_ == UINT64_MAX) return false;
    proof.peer = after; proof.serial = ++serial_; return true;
}
void AuthProofs::remove(std::uintptr_t backend, std::uint16_t handle) noexcept {
    if (!backend || backend != backend_ || !handle || (handle & 63) >= proofs_.size()) return;
    auto& p = proofs_[handle & 63];
    p.peer = {}; p.serial = 0; ++p.epoch;
}
void AuthProofs::clear_backend(std::uintptr_t backend) noexcept {
    if (!backend || backend != backend_) return;
    proofs_ = {}; ++generation_;
}
std::uint64_t AuthProofs::serial(const Peer& p) const noexcept {
    if (!p.handle || (p.handle & 63) >= 16 || p.backend != backend_ ||
        !p.auth_enabled || !p.hosting || p.state != 2) return 0;
    const auto& proof = proofs_[p.handle & 63];
    return proof.peer.same_connection(p) ? proof.serial : 0;
}
namespace {
template<class T> bool read(ReadMemory f, std::uintptr_t p, std::size_t offset, T& value) noexcept {
    return f && p && offset <= UINTPTR_MAX - p && f(p + offset,&value,sizeof(value));
}
}
bool read_query_world(ReadMemory f, std::uintptr_t query, std::uintptr_t& world) noexcept {
    world = 0;
    std::uintptr_t context = 0, execution = 0;
    // Pinned server layout: 0x5c92af derives this embedded state from the world.
    // Test6 captured query->context->execution == authenticated world+0xcc4218
    // on different simulation threads. The context itself lives on the stack.
    constexpr std::uintptr_t execution_offset = 0xcc4218;
    if (!read(f,query,0,context) || !read(f,context,0,execution) || execution <= execution_offset) return false;
    world = execution - execution_offset;
    return true;
}
bool read_owner_chain(ReadMemory f, std::uintptr_t state, std::uintptr_t base,
                      std::size_t slot, OwnerChain& out, OwnerStage* failure) noexcept {
    out = {};
    const auto fail = [failure](OwnerStage stage) { if(failure)*failure=stage;return false; };
    if (slot >= 16 || !state || !base) return fail(OwnerStage::arguments);
    Identity id{};
    std::uintptr_t channels = 0, wrapper = 0, vtable = 0;
    std::uint32_t player = 0, machine = 0;
    if (!read(f,state,0x1b0,id.world) || !id.world) return fail(OwnerStage::world);
    if (!read(f,state,0x1c0 + slot*0x2bb38,id.player) || !id.player || (id.player & 63) != slot) return fail(OwnerStage::player);
    if (!read(f,state,0x1c4 + slot*0x2bb38,id.machine) || !id.machine || (id.machine & 127) >= 17) return fail(OwnerStage::machine);
    if (!read(f,state,0x38,channels) || !read(f,channels,0,id.session)) return fail(OwnerStage::session);
    // Session owns two published snapshots, followed by its internal state.
    // Constructor 0x894766 passes Session+0x4b68 to 0x88df30; the native
    // player/machine lookup offsets are relative to that internal object.
    constexpr std::size_t internal = 0x4b68;
    if (!read(f,id.session,internal + 0x2a60 + slot*0x1d0,player) || player != id.player) return fail(OwnerStage::session_player);
    if (!read(f,id.session,internal + 0x2ab4 + slot*0x1d0,machine) || machine != id.machine) return fail(OwnerStage::session_machine);
    if (!read(f,id.session,internal + 0x4770 + (id.machine & 127)*0xb50,machine) || machine != id.machine) return fail(OwnerStage::machine_record);
    if (!read(f,id.session,internal + 0x4776 + (id.machine & 127)*0xb50,id.peer) || !id.peer || (id.peer & 63) >= 16) return fail(OwnerStage::peer);
    if (!read(f,id.session,0x10,wrapper)) return fail(OwnerStage::wrapper);
    if (!read(f,wrapper,0x18,id.backend)) return fail(OwnerStage::backend);
    if (!read(f,id.backend,0,vtable) || vtable != base + 0x1311048 || id.backend > UINTPTR_MAX - 0xd88) return fail(OwnerStage::backend_type);
    if(failure)*failure=OwnerStage::complete;
    out = {id,id.backend+0xd88}; return true;
}
bool read_peer(ReadMemory f, std::uintptr_t backend, std::uint16_t handle, Peer& out) noexcept {
    out = {};
    if (!backend || backend > UINTPTR_MAX - 0x22b0 || !handle || (handle & 63) >= 16) return false;
    const auto context = backend + 0xd88;
    std::uint16_t actual = 0;
    std::uint8_t enabled = 0, hosting = 0;
    Peer p{};p.backend = backend;p.handle = handle;
    if (!read(f,context,0x70 + (handle & 63)*0x118,actual) || actual != handle ||
        !read(f,context,0x72 + (handle & 63)*0x118,p.steam) || !p.steam ||
        !read(f,context,0x108 + (handle & 63)*0x118,p.state) ||
        !read(f,context,0x1510,enabled) || enabled > 1 ||
        !read(f,context,0x1512,hosting) || hosting > 1) return false;
    p.auth_enabled = enabled != 0; p.hosting = hosting != 0;out = p;return true;
}
}
