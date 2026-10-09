#include "server_command.hpp"

#include <charconv>

namespace xhl::native_time::server {
namespace {
constexpr std::string_view family = "!xhl-time:";

bool parse_hour(std::string_view value, std::uint8_t& output) noexcept {
    if (value.empty() || value.size() > 2 ||
        (value.size() > 1 && value.front() == '0')) return false;
    unsigned hour = 0;
    const auto parsed = std::from_chars(value.data(),
        value.data() + value.size(), hour);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
        hour > 23) return false;
    output = static_cast<std::uint8_t>(hour);
    return true;
}
} // namespace

ParseResult parse_request(std::string_view text) noexcept {
    ParseResult result{};
    if (!text.starts_with(family)) return result;
    result.consumed = true;
    if (text.size() > 64) return result;

    const auto body = text.substr(family.size());
    if (body == "status") result.request.kind = RequestKind::status;
    else if (body == "enable") result.request.kind = RequestKind::enable;
    else if (body == "disable") result.request.kind = RequestKind::disable;
    else if (body.starts_with("hour=")) {
        result.request.kind = RequestKind::set_hour;
        if (!parse_hour(body.substr(5), result.request.hour)) return result;
    } else if (body.starts_with("mode=")) {
        result.request.kind = RequestKind::set_mode;
        const auto mode = body.substr(5);
        if (mode == "normal") result.request.mode = WorldTimeMode::normal;
        else if (mode == "pause") result.request.mode = WorldTimeMode::pause;
        else if (mode == "fast") result.request.mode = WorldTimeMode::fast;
        else return result;
    } else {
        return result;
    }
    result.valid = true;
    return result;
}

} // namespace xhl::native_time::server
