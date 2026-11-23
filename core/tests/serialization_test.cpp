#include "stocksim/serialization.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

using namespace stocksim;

TEST_CASE("StateWriter encodes little-endian fixed-width integers", "[serialization]") {
    StateWriter out;
    out.u32(0x01020304U);
    out.u8(0xAB);
    out.i64(-2);
    const std::vector<std::byte>& bytes = out.bytes();
    REQUIRE(bytes.size() == 4 + 1 + 8);
    CHECK(bytes[0] == std::byte{0x04});
    CHECK(bytes[3] == std::byte{0x01});
    CHECK(bytes[4] == std::byte{0xAB});
    CHECK(bytes[5] == std::byte{0xFE}); // -2 in two's complement, low byte first
    for (std::size_t i = 6; i < bytes.size(); ++i) {
        CHECK(bytes[i] == std::byte{0xFF});
    }
}

TEST_CASE("StateReader round-trips every width including extremes", "[serialization]") {
    StateWriter out;
    out.u8(std::numeric_limits<std::uint8_t>::max());
    out.u32(std::numeric_limits<std::uint32_t>::max());
    out.u64(std::numeric_limits<std::uint64_t>::max());
    out.i64(std::numeric_limits<std::int64_t>::min());
    out.i64(std::numeric_limits<std::int64_t>::max());
    out.boolean(true);
    out.boolean(false);

    StateReader in{out.bytes()};
    CHECK(in.u8() == std::numeric_limits<std::uint8_t>::max());
    CHECK(in.u32() == std::numeric_limits<std::uint32_t>::max());
    CHECK(in.u64() == std::numeric_limits<std::uint64_t>::max());
    CHECK(in.i64() == std::numeric_limits<std::int64_t>::min());
    CHECK(in.i64() == std::numeric_limits<std::int64_t>::max());
    CHECK(in.boolean());
    CHECK_FALSE(in.boolean());
    CHECK(in.ok());
    CHECK(in.remaining() == 0);
}

TEST_CASE("StateReader fails on truncated data and stays failed", "[serialization]") {
    StateWriter out;
    out.u32(7);
    StateReader in{out.bytes()};
    CHECK(in.u64() == 0); // only 4 bytes available
    CHECK_FALSE(in.ok());
    CHECK(in.u8() == 0); // sticky
    CHECK_FALSE(in.ok());
}

TEST_CASE("StateReader rejects non-0/1 booleans", "[serialization]") {
    StateWriter out;
    out.u8(2);
    StateReader in{out.bytes()};
    static_cast<void>(in.boolean());
    CHECK_FALSE(in.ok());
}

TEST_CASE("StateReader count guards against impossible element counts", "[serialization]") {
    StateWriter out;
    out.u64(1'000'000);
    out.u64(0);
    StateReader in{out.bytes()};
    CHECK(in.count(8) == 0);
    CHECK_FALSE(in.ok());

    StateWriter small;
    small.u64(1);
    small.u64(42);
    StateReader fits{small.bytes()};
    CHECK(fits.count(8) == 1);
    CHECK(fits.ok());
}
