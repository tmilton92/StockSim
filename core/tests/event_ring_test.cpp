#include "stocksim/event_ring.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

using namespace stocksim;

namespace {

Event numbered(std::uint64_t seq) {
    Event event;
    event.seq = SequenceNumber{seq};
    return event;
}

} // namespace

TEST_CASE("EventRing rounds capacity up to a power of two", "[event_ring]") {
    CHECK(EventRing{0}.capacity() == 1);
    CHECK(EventRing{1}.capacity() == 1);
    CHECK(EventRing{5}.capacity() == 8);
    CHECK(EventRing{64}.capacity() == 64);
}

TEST_CASE("EventRing is FIFO across wrap-around", "[event_ring]") {
    EventRing ring{4};
    std::uint64_t next_in = 1;
    std::uint64_t next_out = 1;
    std::array<Event, 3> out{};
    for (int round = 0; round < 50; ++round) {
        const std::array<Event, 3> batch{numbered(next_in), numbered(next_in + 1),
                                         numbered(next_in + 2)};
        REQUIRE(ring.try_push(batch));
        next_in += 3;
        REQUIRE(ring.size() == 3);
        REQUIRE(ring.pop(out) == 3);
        for (const Event& event : out) {
            REQUIRE(event.seq == SequenceNumber{next_out++});
        }
    }
    CHECK(ring.size() == 0);
}

TEST_CASE("EventRing pushes are all-or-nothing", "[event_ring]") {
    EventRing ring{4};
    const std::array<Event, 3> three{numbered(1), numbered(2), numbered(3)};
    REQUIRE(ring.try_push(three));
    CHECK(ring.free_space() == 1);
    CHECK_FALSE(ring.try_push(three)); // does not fit: nothing is written
    CHECK(ring.size() == 3);

    std::array<Event, 1> one{};
    REQUIRE(ring.pop(one) == 1);
    CHECK(one[0].seq == SequenceNumber{1});
    CHECK(ring.free_space() == 2);
}

TEST_CASE("EventRing pop takes at most what is queued and what fits", "[event_ring]") {
    EventRing ring{8};
    std::array<Event, 4> out{};
    CHECK(ring.pop(out) == 0);
    REQUIRE(ring.try_push(std::array{numbered(1), numbered(2)}));
    CHECK(ring.pop(out) == 2);
    REQUIRE(ring.try_push(
        std::array{numbered(3), numbered(4), numbered(5), numbered(6), numbered(7), numbered(8)}));
    CHECK(ring.pop(out) == 4);
    CHECK(out[0].seq == SequenceNumber{3});
    CHECK(ring.size() == 2);
}

TEST_CASE("EventRing delivers every event in order across threads", "[event_ring][threads]") {
    constexpr std::uint64_t kTotal = 200'000;
    EventRing ring{64};

    std::thread producer{[&ring] {
        std::uint64_t next = 1;
        while (next <= kTotal) {
            // Variable-size batches, retrying when full (backpressure, never dropping).
            const std::size_t batch_size = static_cast<std::size_t>(next % 4) + 1;
            std::vector<Event> batch;
            for (std::size_t i = 0; i < batch_size && next + i <= kTotal; ++i) {
                batch.push_back(numbered(next + i));
            }
            while (!ring.try_push(batch)) {
                std::this_thread::yield();
            }
            next += batch.size();
        }
    }};

    std::uint64_t expected = 1;
    bool in_order = true;
    std::array<Event, 16> out{};
    while (expected <= kTotal) {
        const std::size_t count = ring.pop(out);
        if (count == 0) {
            std::this_thread::yield();
        }
        for (std::size_t i = 0; i < count; ++i) {
            in_order = in_order && out[i].seq == SequenceNumber{expected};
            ++expected;
        }
    }
    producer.join();
    CHECK(in_order);
    CHECK(expected == kTotal + 1);
    CHECK(ring.size() == 0);
}
