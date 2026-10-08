#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace xhl::flight {
// No identities travel in the payload. The adapter supplies the sender from
// authenticated transport and resolves it to a current game connection.
inline constexpr std::uint32_t client_revision = 1076226;
inline constexpr std::uint32_t server_revision = 1024233;
inline constexpr std::uint64_t lease_ms = 3000, snapshot_ms = 1000;
inline constexpr std::size_t wire_size = 48;
enum class Kind : std::uint8_t { hello = 1, challenge = 2, set = 3, ack = 4 };
enum class Status : std::uint16_t { ok = 0, denied = 1, stale = 2 };
struct Message {
    Kind kind{};
    bool enabled = false;
    Status status{};
    std::uint64_t nonce = 0, token = 0, sequence = 0;
    std::uint32_t build = 0;
};
using Wire = std::array<std::uint8_t, wire_size>;
Wire encode(const Message& message) noexcept;
std::optional<Message> decode(std::span<const std::uint8_t> bytes) noexcept;

struct Allowlist {
    std::array<std::uint64_t, 64> ids{};
    std::size_t count = 0;
    bool valid = false;
    bool contains(std::uint64_t id) const noexcept;
};
// Strict, bounded private server config: schema=1, then one steam_id=... per
// line. Blank lines and # comments are accepted. Any other content denies all.
Allowlist parse_allowlist(std::string_view text) noexcept;

struct Identity {
    std::uintptr_t backend = 0, session = 0, world = 0;
    std::uint32_t player = 0, machine = 0;
    std::uint16_t peer = 0;
    std::uint64_t steam = 0, authentication = 0, lifecycle = 0;
    bool operator==(const Identity&) const = default;
    bool valid(std::size_t slot) const noexcept;
};

// All access is serialized by the native adapter. Times are monotonic ms;
// random challenge tokens are supplied by BCrypt, never by remote messages.
class Sessions {
    struct Player {
        Identity identity{};
        std::uint64_t observed = 0, nonce = 0, token = 0, sequence = 0, expiry = 0;
        bool enabled = false;
    };
    std::array<Player, 16> players_{};
    std::uint64_t next_lifecycle_ = 0;
    Allowlist allowlist_{};
    std::uint64_t config_checked_ = 0;
    bool approved(const Player&, std::uint64_t now) const noexcept;
public:
    void configure(const Allowlist&, std::uint64_t now) noexcept;
    void observe(const std::array<Identity, 16>&, std::uint64_t now) noexcept;
    void clear() noexcept;
    void remove_peer(std::uintptr_t backend, std::uint16_t peer) noexcept;
    void clear_backend(std::uintptr_t backend) noexcept;
    void remove_owner(std::uintptr_t world, std::uint32_t owner) noexcept;
    bool is_approved(std::uint64_t authenticated_sender, std::uint64_t now) const noexcept;
    // Authentication only. Feature allowlists and privilege leases are separate.
    std::optional<Identity> identity_for(std::uint64_t sender, std::uint64_t now) const noexcept;
    std::optional<Identity> identity_for(std::uintptr_t world, std::uint32_t owner, std::uint64_t now) const noexcept;
    bool flying_for(std::uint64_t authenticated_sender, std::uint64_t now) const noexcept;
    std::optional<Message> receive(std::uint64_t authenticated_sender, const Message&,
                                   std::uint64_t now, std::uint64_t random_token) noexcept;
    bool can_fly(std::uintptr_t world, std::uint32_t owner, std::uint64_t now) const noexcept;
};

// Transport calls reset whenever the underlying game connection changes.
class ClientSession {
    std::uint64_t nonce_ = 0, token_ = 0, sequence_ = 0, expires_ = 0;
    std::uint64_t sent_at_ = 0;
    bool desired_ = false, approved_ = false;
public:
    void reset(std::uint64_t random_nonce = 0) noexcept;
    Message hello() const noexcept;
    std::optional<Message> request(bool enabled, std::uint64_t now) noexcept;
    bool receive(const Message&, std::uint64_t now) noexcept;
    bool can_fly(std::uint64_t now) const noexcept;
    bool ready() const noexcept { return token_ != 0; }
};
}
