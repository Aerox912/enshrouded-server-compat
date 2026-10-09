#pragma once

#include "read_only_observer.hpp"

#include <windows.h>

#include <cstddef>

namespace xhl::native_time::observer {

// This adapter is opt-in and observational only. Its install function is not
// called by static initialization or by the server runtime. Installation also
// requires XHL_ENABLE_NATIVE_TIME_OBSERVER=1 in the dedicated server process.
bool install_local_read_only_server_observer(HMODULE server) noexcept;
bool stop_local_read_only_server_observer() noexcept;
bool local_read_only_server_observer_ready() noexcept;
Statistics local_read_only_server_observer_statistics() noexcept;
std::size_t copy_local_read_only_server_observer_events(
    std::span<Event> destination) noexcept;

// Receives one UTF-8 JSONL line at a time. The line buffer is temporary; a
// writer must consume it synchronously and return false to abort. Dumping is
// available only after successful stop, and this API never opens/writes files.
using JsonLineWriter = bool (*)(void* context, const char* bytes,
    std::size_t size) noexcept;
bool dump_local_read_only_server_observer_jsonl(JsonLineWriter writer,
    void* context) noexcept;

} // namespace xhl::native_time::observer
