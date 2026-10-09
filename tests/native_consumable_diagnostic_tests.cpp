#include "../src/native_consumable_diagnostic.hpp"
#include <array>
#include <cstdint>
#include <cstdio>

using namespace xhl::native_consumable_diagnostic;

namespace {
int failures = 0;

void check(bool condition, const char* expression) {
    if (condition) return;
    std::fprintf(stderr, "FAIL %s\n", expression);
    ++failures;
}
#define CHECK(expression) check((expression), #expression)

void test_bounded_ring() {
    FiredObservationRing ring;
    for (std::uint64_t i = 1; i <= observation_capacity + 6; ++i) {
        FiredObservation record{};
        record.caller_return_rva = 0x30ce9;
        record.removal_owner = static_cast<std::uint32_t>(i);
        ring.append(record);
    }

    std::array<FiredObservation, observation_capacity> records{};
    const auto count = ring.read_after(0, records.data(), records.size());
    CHECK(count == observation_capacity);
    CHECK(records.front().sequence == 7);
    CHECK(records.back().sequence == observation_capacity + 6);
    CHECK(records.front().caller_return_rva == 0x30ce9);
    CHECK(records.back().removal_owner == observation_capacity + 6);

    std::array<FiredObservation, 2> recent{};
    const auto recent_count = ring.read_after(observation_capacity + 4,
        recent.data(), recent.size());
    CHECK(recent_count == 2);
    CHECK(recent[0].sequence == observation_capacity + 5);
    CHECK(recent[1].sequence == observation_capacity + 6);
    CHECK(!FiredObservation{}.callback_registration_proven);
    CHECK(!enabled_by_default);
}
} // namespace

int main() {
    test_bounded_ring();
    if (failures) {
        std::fprintf(stderr, "%d fired diagnostic checks failed\n", failures);
        return 1;
    }
    std::puts("PASS bounded fired diagnostic checks");
    return 0;
}
