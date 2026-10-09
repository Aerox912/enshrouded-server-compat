#include <cstdint>
// Only the isolated DLL pin/lifetime probe uses this inert route. Publication
// and C++/SEH unwind tests use the separate EXE's fault-capable route.
extern "C" void creative_g_dispatch_route(std::uintptr_t*,void*) noexcept {}
