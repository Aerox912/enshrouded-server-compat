#pragma once

#include "time_control.hpp"

#include <cstdint>
#include <string_view>

namespace xhl::native_time::server {

enum class RequestKind : std::uint8_t {
    invalid, status, enable, disable, set_hour, set_mode
};

struct Request {
    RequestKind kind = RequestKind::invalid;
    std::uint8_t hour = 0;
    WorldTimeMode mode = WorldTimeMode::unspecified;
};

struct ParseResult {
    bool consumed = false;
    bool valid = false;
    Request request{};
};

// Private authenticated Steam-channel commands. Messages outside the family
// are untouched; malformed messages in the family are consumed and rejected.
ParseResult parse_request(std::string_view text) noexcept;

} // namespace xhl::native_time::server
