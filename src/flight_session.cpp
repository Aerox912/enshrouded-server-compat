#include "flight_session.hpp"
#include <algorithm>
#include <charconv>
#include <limits>

namespace xhl::flight {
namespace {
constexpr std::array<std::uint8_t, 8> magic{'A','E','R','O','F','L','Y','1'};
void put(Wire& b, std::size_t at, std::uint64_t n, std::size_t count) noexcept {
    for (std::size_t i = 0; i < count; ++i) b[at + i] = static_cast<std::uint8_t>(n >> (i * 8));
}
std::uint64_t get(std::span<const std::uint8_t> b, std::size_t at, std::size_t count) noexcept {
    std::uint64_t n = 0;
    for (std::size_t i = 0; i < count; ++i) n |= std::uint64_t(b[at + i]) << (i * 8);
    return n;
}
bool fresh(std::uint64_t then, std::uint64_t now, std::uint64_t duration) noexcept {
    return now >= then && now - then < duration;
}
bool individual(std::uint64_t id) noexcept {
    return (id >> 32) == 0x01100001 && static_cast<std::uint32_t>(id) != 0;
}
std::string_view trim(std::string_view line) noexcept {
    const auto first = line.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) return {};
    return line.substr(first, line.find_last_not_of(" \t\r") - first + 1);
}
}
Wire encode(const Message& m) noexcept {
    Wire b{};
    std::copy(magic.begin(), magic.end(), b.begin());
    put(b, 8, 1, 2); put(b, 10, wire_size, 2);
    b[12] = static_cast<std::uint8_t>(m.kind); b[13] = m.enabled ? 1 : 0;
    put(b, 14, static_cast<std::uint16_t>(m.status), 2);
    put(b, 16, m.nonce, 8); put(b, 24, m.token, 8); put(b, 32, m.sequence, 8);
    put(b, 40, m.build, 4);
    return b;
}
std::optional<Message> decode(std::span<const std::uint8_t> b) noexcept {
    if (b.size() != wire_size || !std::equal(magic.begin(), magic.end(), b.begin()) ||
        get(b, 8, 2) != 1 || get(b, 10, 2) != wire_size || b[12] < 1 || b[12] > 4 ||
        b[13] > 1 || get(b, 14, 2) > 2 || get(b, 44, 4) != 0) return {};
    return Message{static_cast<Kind>(b[12]), b[13] != 0, static_cast<Status>(get(b, 14, 2)),
                   get(b, 16, 8), get(b, 24, 8), get(b, 32, 8), static_cast<std::uint32_t>(get(b, 40, 4))};
}
bool Allowlist::contains(std::uint64_t id) const noexcept {
    return valid && count <= ids.size() && std::find(ids.begin(), ids.begin() + count, id) != ids.begin() + count;
}
Allowlist parse_allowlist(std::string_view text) noexcept {
    Allowlist result;
    if (text.size() > 8192 || text.find('\0') != std::string_view::npos) return {};
    bool schema = false;
    while (!text.empty()) {
        const auto end = text.find('\n');
        auto line = trim(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (line.empty() || line.front() == '#') continue;
        if (line == "schema=1" && !schema && result.count == 0) { schema = true; continue; }
        constexpr std::string_view prefix = "steam_id=";
        if (!schema || !line.starts_with(prefix) || result.count == result.ids.size()) return {};
        line.remove_prefix(prefix.size());
        std::uint64_t id = 0;
        const auto parsed = std::from_chars(line.data(), line.data() + line.size(), id);
        if (parsed.ec != std::errc{} || parsed.ptr != line.data() + line.size() || !individual(id) ||
            std::find(result.ids.begin(), result.ids.begin() + result.count, id) != result.ids.begin() + result.count) return {};
        result.ids[result.count++] = id;
    }
    result.valid = schema;
    return result;
}
bool Identity::valid(std::size_t slot) const noexcept {
    return backend && session && world && player && (player & 63) == slot && machine &&
           (machine & 127) < 17 && peer && (peer & 63) < 16 && individual(steam) && authentication;
}
bool Sessions::approved(const Player& p, std::uint64_t now) const noexcept {
    return p.identity.authentication && allowlist_.contains(p.identity.steam) &&
           fresh(config_checked_, now, 2000) && fresh(p.observed, now, snapshot_ms);
}
void Sessions::configure(const Allowlist& list, std::uint64_t now) noexcept {
    allowlist_ = list; config_checked_ = now;
    for (auto& p : players_) if (!list.contains(p.identity.steam)) {
        p.nonce = p.token = p.sequence = p.expiry = 0; p.enabled = false;
    }
}
void Sessions::observe(const std::array<Identity, 16>& identities, std::uint64_t now) noexcept {
    for (std::size_t slot = 0; slot < players_.size(); ++slot) {
        auto id = identities[slot];
        auto& p = players_[slot];
        if (!id.valid(slot)) { p = {}; continue; }
        // The native mapping does not supply a lifecycle. Issue a new one after
        // every removal/reset, even when all native handles are reused immediately.
        id.lifecycle = p.identity.lifecycle;
        if (!(p.identity == id)) { p = {}; id.lifecycle = ++next_lifecycle_; p.identity = id; }
        p.observed = now;
    }
    // Reject all sides of an ambiguous mapping, including an existing lease.
    std::array<bool,16> ambiguous{};
    for(std::size_t i=0;i<identities.size();++i)
        for(std::size_t j=i+1;j<identities.size();++j)
            if(identities[i].steam && identities[i].steam==identities[j].steam)ambiguous[i]=ambiguous[j]=true;
    for(std::size_t i=0;i<players_.size();++i)if(ambiguous[i])players_[i]={};
}
void Sessions::clear() noexcept { players_ = {}; }
std::optional<ActorObservation> Sessions::begin_actor_observation(std::uintptr_t world,std::uint32_t owner,std::uint64_t now) noexcept {
    if(!world || owner<1 || owner>players_.size())return {};
    auto& p=players_[owner-1];
    if(p.identity.world!=world || !p.identity.valid(owner-1) || !fresh(p.observed,now,snapshot_ms))return {};
    if(next_actor_ticket_==UINT64_MAX) {
        // Saturation cannot reuse an observation identity or retain live evidence.
        if(p.actor_known)p.identity.lifecycle=++next_lifecycle_;
        p.actor_ticket=0;p.actor_known=p.actor_alive=false;return {};
    }
    p.actor_ticket=++next_actor_ticket_;
    return ActorObservation{p.identity,p.actor_ticket,now};
}
bool Sessions::observe_actor(const ActorObservation& sample,std::optional<std::uint64_t> state,std::uint64_t now) noexcept {
    const auto slot=sample.identity.player&63;
    if(slot>=players_.size() || !sample.ticket)return false;
    auto& p=players_[slot];
    if(p.identity!=sample.identity || p.actor_ticket!=sample.ticket ||
        now<sample.sampled_at ||
        (p.actor_sampled && sample.sampled_at<p.actor_seen))return false;
    p.actor_ticket=0; // Every ticket is single-use, even if lifecycle stays the same.
    // Ownership may expire between sampling and publication. A matching
    // failure/dead sample must still invalidate old evidence before a refresh.
    if(!fresh(p.observed,now,snapshot_ms) || !fresh(sample.sampled_at,now,snapshot_ms))state.reset();
    constexpr auto suspended=(std::uint64_t{1}<<7)|(std::uint64_t{1}<<12); // Dead / Spawning
    const bool alive=state && (*state&suspended)==0;
    const bool changed=state ? p.actor_sampled && (!p.actor_known || p.actor_alive!=alive || !fresh(p.actor_seen,now,snapshot_ms)) : p.actor_known;
    if(changed)p.identity.lifecycle=++next_lifecycle_;
    p.actor_sampled=true;p.actor_known=state.has_value();p.actor_alive=alive;p.actor_seen=sample.sampled_at;
    return changed;
}
bool Sessions::alive_for(std::uintptr_t world,std::uint32_t owner,std::uint64_t now) const noexcept {
    if(!world || owner<1 || owner>players_.size())return false;
    const auto& p=players_[owner-1];
    return p.identity.world==world && p.actor_known && p.actor_alive &&
        fresh(p.observed,now,snapshot_ms) && fresh(p.actor_seen,now,snapshot_ms);
}
bool Sessions::alive_for(const Identity& id,std::uint64_t now) const noexcept {
    return actor_alive_for(id,now).value_or(false);
}
std::optional<bool> Sessions::actor_alive_for(const Identity& id,std::uint64_t now) const noexcept {
    const auto slot=id.player&63;
    if(slot>=players_.size())return {};
    const auto& p=players_[slot];
    if(p.identity!=id || !p.actor_known || !fresh(p.observed,now,snapshot_ms) ||
        !fresh(p.actor_seen,now,snapshot_ms))return {};
    return p.actor_alive;
}
void Sessions::remove_peer(std::uintptr_t backend, std::uint16_t peer) noexcept {
    if(!backend || !peer)return;
    for (auto& p : players_) if (p.identity.backend == backend && p.identity.peer == peer) p = {};
}
void Sessions::clear_backend(std::uintptr_t backend) noexcept {
    if(!backend)return;
    for (auto& p : players_) if (p.identity.backend == backend) p = {};
}
void Sessions::remove_owner(std::uintptr_t world, std::uint32_t owner) noexcept {
    if (owner >= 1 && owner <= players_.size() && players_[owner - 1].identity.world == world) players_[owner - 1] = {};
}
bool Sessions::is_approved(std::uint64_t sender, std::uint64_t now) const noexcept {
    if(!sender)return false;
    const Player* found=nullptr;
    for(const auto& p:players_)if(p.identity.steam==sender){if(found)return false;found=&p;}
    return found && approved(*found,now);
}
std::optional<Identity> Sessions::identity_for(std::uint64_t sender, std::uint64_t now) const noexcept {
    if(!sender)return {};
    std::optional<Identity> found;
    for(const auto& p:players_)if(p.identity.steam==sender) {
        if(found || !p.identity.authentication || !fresh(p.observed,now,snapshot_ms))return {};
        found=p.identity;
    }
    return found;
}
std::optional<Identity> Sessions::identity_for(std::uintptr_t world,std::uint32_t owner,std::uint64_t now) const noexcept {
    if(!world || owner<1 || owner>players_.size())return {};
    const auto& p=players_[owner-1];
    if(p.identity.world!=world || !p.identity.valid(owner-1) || !fresh(p.observed,now,snapshot_ms))return {};
    return p.identity;
}
bool Sessions::flying_for(std::uint64_t sender, std::uint64_t now) const noexcept {
    if(!is_approved(sender,now))return false;
    for(std::size_t i=0;i<players_.size();++i){const auto& p=players_[i];if(p.identity.steam==sender)return p.enabled && now<p.expiry && alive_for(p.identity.world,static_cast<std::uint32_t>(i+1),now);}
    return false;
}
std::optional<Message> Sessions::receive(std::uint64_t sender, const Message& m,
                                      std::uint64_t now, std::uint64_t token) noexcept {
    if (!sender || !m.nonce || m.build != client_revision || m.status != Status::ok) return {};
    Player* player = nullptr;
    for (auto& candidate : players_) if (candidate.identity.steam == sender) {
        if (player) return {}; // Ambiguous ownership fails closed.
        player = &candidate;
    }
    if (!player || !approved(*player, now)) return {};
    auto& p = *player;
    if (m.kind == Kind::hello && !m.token && !m.sequence && !m.enabled) {
        if (p.nonce != m.nonce || !p.token) {
            if (!token) return {};
            p.nonce = m.nonce; p.token = token; p.sequence = p.expiry = 0; p.enabled = false;
        }
        return Message{Kind::challenge, false, Status::ok, p.nonce, p.token, 0, server_revision};
    }
    if (m.kind != Kind::set || !m.sequence || !m.token || p.nonce != m.nonce || p.token != m.token) return {};
    if (m.sequence < p.sequence || (m.sequence == p.sequence && m.enabled != p.enabled)) return {};
    if (m.sequence > p.sequence) {
        p.sequence = m.sequence; p.enabled = m.enabled;
        p.expiry = now <= UINT64_MAX - lease_ms ? now + lease_ms : now;
    }
    // Replaying a duplicate never extends a lease or reactivates expired flight.
    return Message{Kind::ack, p.enabled && now < p.expiry && alive_for(p.identity.world,(p.identity.player&63)+1,now), Status::ok, p.nonce, p.token, p.sequence, server_revision};
}
bool Sessions::can_fly(std::uintptr_t world, std::uint32_t owner, std::uint64_t now) const noexcept {
    if (!world || owner < 1 || owner > players_.size()) return false;
    const auto& p = players_[owner - 1];
    return alive_for(world,owner,now) && approved(p, now) && p.enabled && now < p.expiry;
}
void ClientSession::reset(std::uint64_t nonce) noexcept {
    nonce_ = nonce; token_ = sequence_ = expires_ = sent_at_ = 0; desired_ = approved_ = false;
}
Message ClientSession::hello() const noexcept { return {Kind::hello, false, Status::ok, nonce_, 0, 0, client_revision}; }
std::optional<Message> ClientSession::request(bool enabled, std::uint64_t now) noexcept {
    if (!nonce_ || !token_ || sequence_ == UINT64_MAX) return {};
    desired_ = enabled;
    if (!enabled) { approved_ = false; expires_ = 0; }
    sent_at_ = now;
    return Message{Kind::set, enabled, Status::ok, nonce_, token_, ++sequence_, client_revision};
}
bool ClientSession::receive(const Message& m, std::uint64_t now) noexcept {
    if (!nonce_ || m.nonce != nonce_ || !m.token || m.build != server_revision || m.status != Status::ok) return false;
    if (m.kind == Kind::challenge && !m.enabled && !m.sequence) {
        // A repeated hello response cannot replace a running session's token.
        if (token_) return token_ == m.token;
        token_ = m.token; return true;
    }
    if (m.kind != Kind::ack || !sequence_ || m.token != token_ || m.sequence != sequence_ ||
        (m.enabled && !desired_) || !fresh(sent_at_, now, lease_ms)) return false;
    approved_ = m.enabled;
    // Anchor expiry to request SEND time, not delayed response/replay arrival.
    expires_ = sent_at_ <= UINT64_MAX - lease_ms ? sent_at_ + lease_ms : sent_at_;
    return true;
}
bool ClientSession::can_fly(std::uint64_t now) const noexcept { return approved_ && now >= sent_at_ && now < expires_; }
}
