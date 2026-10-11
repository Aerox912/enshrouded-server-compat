#pragma once
#include <windows.h>
#include <string>

namespace xhl {
using Logger = void (*)(const char*);
inline constexpr char server_hash[] = "001c1b40ed091d8c1aee583adde3800d7c858ae2c7f4dff54fca2938b2be1637";
inline constexpr char radius_plugin_hash[] = "4921a0009dbb1f9fcca79be355a4e3fb70481be4b811b1f1eba1c4bcbab936f7";
inline constexpr char xp_hash[] = "1971830b0457ca97d3dbcdb8f681c45fbebcf23813d77a58add793249490e84c";
std::wstring module_path(HMODULE module);
bool verify_file(const std::wstring& path, const char* expected);
bool validate_server(HMODULE game);
bool start_extras(HMODULE game, const std::wstring& root, Logger logger);
// One initialization per process. Keeps the native plugin and trampolines loaded
// until process exit. Failure leaves ordinary server mining available.
bool start(HMODULE game, const std::wstring& plugin_path, Logger logger);
}
