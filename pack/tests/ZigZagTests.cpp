#include <cstdint>
#include <limits>
#include <random>

#include "ao/pack/ZigZag.h"
#include <catch2/catch_all.hpp>

using namespace ao::pack;

// Helpers to keep the tests readable.
static constexpr int64_t I64_MIN = std::numeric_limits<int64_t>::min();
static constexpr int64_t I64_MAX = std::numeric_limits<int64_t>::max();
static constexpr uint64_t U64_MAX = std::numeric_limits<uint64_t>::max();

TEST_CASE("ZigZag golden vectors (small values)", "[zigzag]") {
    REQUIRE(encodeZigZag(0) == 0ULL);
    REQUIRE(encodeZigZag(-1) == 1ULL);
    REQUIRE(encodeZigZag(1) == 2ULL);
    REQUIRE(encodeZigZag(-2) == 3ULL);
    REQUIRE(encodeZigZag(2) == 4ULL);

    REQUIRE(decodeZigZag(0ULL) == 0);
    REQUIRE(decodeZigZag(1ULL) == -1);
    REQUIRE(decodeZigZag(2ULL) == 1);
    REQUIRE(decodeZigZag(3ULL) == -2);
    REQUIRE(decodeZigZag(4ULL) == 2);
}

TEST_CASE("ZigZag known boundary encodings", "[zigzag][limits]") {
    // These are standard ZigZag properties for 64-bit:
    //  max positive => 0x...FE
    //  min negative => 0x...FF
    REQUIRE(encodeZigZag(I64_MAX) == 0xFFFFFFFFFFFFFFFEULL);
    REQUIRE(encodeZigZag(I64_MIN) == 0xFFFFFFFFFFFFFFFFULL);

    // Also check adjacent values around extremes.
    REQUIRE(encodeZigZag(I64_MAX - 1) == 0xFFFFFFFFFFFFFFFCULL);
    REQUIRE(encodeZigZag(I64_MIN + 1) == 0xFFFFFFFFFFFFFFFDULL);

    // Decoding those should return the originals.
    REQUIRE(decodeZigZag(0xFFFFFFFFFFFFFFFEULL) == I64_MAX);
    REQUIRE(decodeZigZag(0xFFFFFFFFFFFFFFFFULL) == I64_MIN);
    REQUIRE(decodeZigZag(0xFFFFFFFFFFFFFFFCULL) == I64_MAX - 1);
    REQUIRE(decodeZigZag(0xFFFFFFFFFFFFFFFDULL) == I64_MIN + 1);
}

TEST_CASE("ZigZag round-trip property on a focused set near limits",
          "[zigzag][limits]") {
    // Dense sampling around both ends to catch shift/sign edge cases.
    const int64_t vals[] = {I64_MIN,
                            I64_MIN + 1,
                            I64_MIN + 2,
                            I64_MIN + 3,
                            I64_MIN + 4,
                            I64_MIN + 7,
                            I64_MIN + 8,
                            I64_MIN + 15,
                            I64_MIN + 16,
                            -16,
                            -15,
                            -8,
                            -7,
                            -4,
                            -3,
                            -2,
                            -1,
                            0,
                            1,
                            2,
                            3,
                            4,
                            7,
                            8,
                            15,
                            16,
                            I64_MAX - 16,
                            I64_MAX - 15,
                            I64_MAX - 8,
                            I64_MAX - 7,
                            I64_MAX - 4,
                            I64_MAX - 3,
                            I64_MAX - 2,
                            I64_MAX - 1,
                            I64_MAX};

    for (int64_t x : vals) {
        INFO("x = " << x);
        const uint64_t enc = encodeZigZag(x);
        const int64_t dec = decodeZigZag(enc);
        REQUIRE(dec == x);
    }
}

TEST_CASE(
    "ZigZag decode->encode is identity for key uint64 patterns (near uint "
    "limits)",
    "[zigzag][limits]") {
    const uint64_t patterns[] = {0ULL,
                                 1ULL,
                                 2ULL,
                                 3ULL,
                                 4ULL,
                                 5ULL,
                                 0x7FFFFFFFFFFFFFFFULL,
                                 0x8000000000000000ULL,
                                 0xFFFFFFFFFFFFFFF0ULL,
                                 0xFFFFFFFFFFFFFFF1ULL,
                                 0xFFFFFFFFFFFFFFF2ULL,
                                 0xFFFFFFFFFFFFFFFCULL,
                                 0xFFFFFFFFFFFFFFFDULL,
                                 0xFFFFFFFFFFFFFFFEULL,
                                 0xFFFFFFFFFFFFFFFFULL};

    for (uint64_t n : patterns) {
        INFO("n = 0x" << std::hex << n);
        const int64_t dec = decodeZigZag(n);
        const uint64_t rec = encodeZigZag(dec);
        REQUIRE(rec == n);
    }
}

TEST_CASE(
    "ZigZag parity property: non-negative encodes to even, negative to odd",
    "[zigzag]") {
    // Include extremes and near-extremes.
    const int64_t nonneg[] = {0, 1, 2, 3, 7, 8, 15, 16, I64_MAX - 1, I64_MAX};
    for (int64_t x : nonneg) {
        INFO("x = " << x);
        REQUIRE((encodeZigZag(x) & 1ULL) == 0ULL);
    }

    const int64_t neg[] = {-1, -2, -3, -7, -8, -15, -16, I64_MIN, I64_MIN + 1};
    for (int64_t x : neg) {
        INFO("x = " << x);
        REQUIRE((encodeZigZag(x) & 1ULL) == 1ULL);
    }
}

TEST_CASE("ZigZag monotonic-by-magnitude around zero (sanity)", "[zigzag]") {
    // encode(0)=0, encode(-1)=1, encode(1)=2, encode(-2)=3, encode(2)=4, ...
    for (int64_t k = 0; k <= 10000; ++k) {
        INFO("k = " << k);
        REQUIRE(encodeZigZag(k) == static_cast<uint64_t>(k) * 2ULL);
        if (k > 0) {
            REQUIRE(encodeZigZag(-k) == static_cast<uint64_t>(k) * 2ULL - 1ULL);
        }
    }
}

TEST_CASE("ZigZag randomized round-trip with extra weight near limits",
          "[zigzag][fuzz][limits]") {
    std::mt19937_64 rng(0xC0FFEEULL);

    auto check = [](int64_t x) {
        INFO("x = " << x);
        REQUIRE(decodeZigZag(encodeZigZag(x)) == x);
    };

    // Strong focus near min/max with small offsets
    for (int i = 0; i < 20000; ++i) {
        uint64_t off = rng() & 0xFFFFULL;  // 0..65535
        check(static_cast<int64_t>(I64_MIN + static_cast<int64_t>(off)));
        check(static_cast<int64_t>(I64_MAX - static_cast<int64_t>(off)));
    }

    // Broad coverage too
    std::uniform_int_distribution<int64_t> dist(I64_MIN, I64_MAX);
    for (int i = 0; i < 200000; ++i) {
        check(dist(rng));
    }
}
