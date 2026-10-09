#include "server_command.hpp"

#include <iostream>
#include <string>
#include <string_view>

namespace {
unsigned checks = 0;
bool check(bool condition, const char* message) {
    ++checks;
    if (!condition) std::cerr << "FAIL " << message << '\n';
    return condition;
}
}

int main() {
    using namespace xhl::native_time;
    bool ok = true;
    auto parsed = server::parse_request("!xhl-time:status");
    ok &= check(parsed.consumed && parsed.valid &&
        parsed.request.kind == server::RequestKind::status,
        "status is recognized only inside the private command family");
    parsed = server::parse_request("!xhl-time:enable");
    ok &= check(parsed.valid && parsed.request.kind == server::RequestKind::enable,
        "enable request is recognized");
    parsed = server::parse_request("!xhl-time:disable");
    ok &= check(parsed.valid && parsed.request.kind == server::RequestKind::disable,
        "disable request is recognized");
    parsed = server::parse_request("!xhl-time:hour=0");
    ok &= check(parsed.valid && parsed.request.kind == server::RequestKind::set_hour &&
        parsed.request.hour == 0, "midnight hour is accepted");
    parsed = server::parse_request("!xhl-time:hour=23");
    ok &= check(parsed.valid && parsed.request.hour == 23,
        "the last hour of the day is accepted");
    parsed = server::parse_request("!xhl-time:mode=fast");
    ok &= check(parsed.valid && parsed.request.mode == WorldTimeMode::fast,
        "fast mode is recognized");
    parsed = server::parse_request("!xhl-time:mode=pause");
    ok &= check(parsed.valid && parsed.request.mode == WorldTimeMode::pause,
        "pause mode is recognized");
    parsed = server::parse_request("!xhl-time:mode=normal");
    ok &= check(parsed.valid && parsed.request.mode == WorldTimeMode::normal,
        "normal mode is recognized");

    for (const auto invalid : {"!xhl-time:hour=24", "!xhl-time:hour=-1",
             "!xhl-time:hour=01", "!xhl-time:hour=1x", "!xhl-time:mode=1",
             "!xhl-time:mode=", "!xhl-time:enable:extra", "!xhl-time:",
             "!xhl-time:hour=999999999999999999999999999999999999"}) {
        parsed = server::parse_request(invalid);
        ok &= check(parsed.consumed && !parsed.valid,
            "malformed command-family requests are consumed and rejected");
    }
    parsed = server::parse_request(std::string(65, 'a'));
    ok &= check(!parsed.consumed,
        "ordinary messages outside the command family pass through");
    parsed = server::parse_request("!xhl-time:status" + std::string(64, 'x'));
    ok &= check(parsed.consumed && !parsed.valid,
        "oversized private command messages are consumed and rejected");

    std::cout << (ok ? "PASS native time server command parser\n" :
        "FAIL native time server command parser\n");
    return ok ? 0 : 1;
}
