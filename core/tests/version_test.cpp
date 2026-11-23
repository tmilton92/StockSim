#include "stocksim/version.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

TEST_CASE("core version string matches the numeric version", "[version]") {
    const std::string expected = std::to_string(stocksim::kCoreVersion.major) + "." +
                                 std::to_string(stocksim::kCoreVersion.minor) + "." +
                                 std::to_string(stocksim::kCoreVersion.patch);
    REQUIRE(stocksim::kCoreVersionString == expected);
}
