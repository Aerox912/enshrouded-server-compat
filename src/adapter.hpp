#pragma once
#include <windows.h>
#include <string>

namespace xhl {
using Logger = void (*)(const char*);
inline constexpr char server_hash[] = "001c1b40ed091d8c1aee583adde3800d7c858ae2c7f4dff54fca2938b2be1637";
inline constexpr char plugin_hash[] = "6d31c439fda536aad2078683a244e2d05f685e5e3fba2d5467950fff613f5bcc";
inline constexpr char xp_hash[] = "1971830b0457ca97d3dbcdb8f681c45fbebcf23813d77a58add793249490e84c";
std::wstring module_path(HMODULE module);
bool verify_file(const std::wstring& path, const char* expected);
bool validate_server(HMODULE game);
bool start_extras(HMODULE game, const std::wstring& root, Logger logger);
// One initialization per process. Keeps the native plugin and trampolines loaded
// until process exit. Failure leaves ordinary server mining available.
bool start(HMODULE game, const std::wstring& plugin_path, Logger logger);
}
