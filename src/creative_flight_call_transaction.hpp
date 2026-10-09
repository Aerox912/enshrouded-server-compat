#pragma once
#include "creative_flight_call_install.hpp"
#include <array>
#include <cstddef>
#include <span>
namespace xhl::creative_flight::call_install::detail {
struct Plan {
    std::uintptr_t site=0,slot=0,bridge=0;
    std::array<std::uint8_t,8> before{},after{};
};
inline constexpr std::array<std::uint8_t,8> displaced{0x48,0x8b,0x45,0x38,0x0f,0xb6,0x48,0x3d};
bool make_plan(std::uintptr_t site,std::uintptr_t slot,std::uintptr_t bridge,Plan&) noexcept;
struct Recovery {
    Plan plan{};Result last{};
    std::array<std::uint8_t,8> owned_fragment{};
    std::uint32_t original_protection=0;std::size_t opened=0;
    bool active=false,modified=false,fragment_known=false,protection_known=false;
    bool cache_proven=false,protection_proven=false;
};
struct Thread { std::uint32_t id=0; std::uintptr_t handle=0; bool suspended=false; };
// All buffers and API addresses are ready before run(). No allocation, logging,
// loader work, handle opening or closing occurs from first suspend to last resume.
// enumerate uses a fixed caller buffer. New IDs cause resume, reopen, retry.
struct Ops {
    void* context=nullptr;
    bool (*enumerate)(void*,std::span<std::uint32_t>,std::size_t&)=nullptr;
    bool (*open)(void*,std::uint32_t,std::uintptr_t&)=nullptr;
    void (*close)(void*,std::uintptr_t)=nullptr;
    bool (*suspend)(void*,std::uintptr_t)=nullptr;
    bool (*resume)(void*,std::uintptr_t)=nullptr;
    bool (*rip)(void*,std::uintptr_t,std::uintptr_t&)=nullptr;
    bool (*proof)(void*)=nullptr;
    bool (*read)(void*,std::uintptr_t,std::span<std::uint8_t>)=nullptr;
    bool (*protect)(void*,std::uintptr_t,std::size_t,std::uint32_t,std::uint32_t&)=nullptr;
    bool (*write)(void*,std::uintptr_t,std::span<const std::uint8_t>,std::size_t&)=nullptr;
    bool (*flush)(void*,std::uintptr_t,std::size_t)=nullptr;
    Recovery* recovery=nullptr; // Required durable caller-owned state.
};
// Private production engine; injectable solely for isolated selftests.
Result run(const Plan&,const Ops&,std::span<Thread>,std::span<std::uint32_t>,
           unsigned retry_limit=4) noexcept;
// One attempt only; never opens/suspends threads or overwrites unowned bytes.
Result recover(const Ops&,std::span<Thread>) noexcept;
}
#if defined(CREATIVE_CALL_INSTALL_SELFTEST)
namespace xhl::creative_flight::call_install::detail {
// Isolated fixture only. Uses production enumeration, proof callback, slot,
// memory protection and write wrappers; absent from production builds.
Result publish_native_fixture(std::uintptr_t site,std::uintptr_t bridge,
                             bool (*proof)(void*),void* proof_context) noexcept;
bool native_atomic_probe(std::uintptr_t,const std::array<std::uint8_t,16>&,const std::array<std::uint8_t,16>&) noexcept;
bool pin_native_fixture_bridge(std::uintptr_t) noexcept;
bool reject_fixture_file_identity(Image) noexcept;
struct SnapshotEvidence {std::uint32_t windows_build=0,bytes=0,process_records=0,process_threads=0;};
bool native_thread_snapshot(std::span<std::uint32_t>,std::size_t&,SnapshotEvidence* =nullptr) noexcept;
}
#endif
