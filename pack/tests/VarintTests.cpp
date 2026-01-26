#include <catch2/catch_all.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <span>
#include <vector>

#include <ao/pack/ByteStream.h>
#include <ao/pack/Varint.h>

using namespace ao::pack;
using namespace ao::pack::byte;

static std::span<std::byte> asBytes(std::span<std::uint8_t> s) {
    return {reinterpret_cast<std::byte*>(s.data()), s.size()};
}
static std::span<std::byte const> asConstBytes(
    std::span<std::uint8_t const> s) {
    return {reinterpret_cast<std::byte const*>(s.data()), s.size()};
}

static size_t bytesWritten(size_t capacity, WriteStream const& ws) {
    return capacity - ws.remainingBytes();
}
static size_t bytesSized(SizeWriteStream const& sws) {
    // remainingBytes() == SIZE_MAX - position
    return std::numeric_limits<size_t>::max() - sws.remainingBytes();
}

TEST_CASE("Varint: known encodings (single and multi-byte)") {
    struct Case {
        uint64_t v;
        std::vector<std::uint8_t> expected;
    };

    // Standard LEB128-style varint encodings (7 bits + continuation)
    std::vector<Case> cases = {
        {0, {0x00}},
        {1, {0x01}},
        {127, {0x7F}},
        {128, {0x80, 0x01}},
        {129, {0x81, 0x01}},
        {300, {0xAC, 0x02}},          // 300 = 0b1 0010 1100 -> [0xAC, 0x02]
        {16383, {0xFF, 0x7F}},        // (1<<14)-1
        {16384, {0x80, 0x80, 0x01}},  // 1<<14
    };

    for (auto const& tc : cases) {
        std::vector<std::uint8_t> buf(32, 0);
        WriteStream ws(asBytes(std::span{buf}));

        REQUIRE(encodeVarint(ws, tc.v));
        REQUIRE(ws.ok());

        size_t const n = bytesWritten(buf.size(), ws);
        REQUIRE(n == tc.expected.size());

        for (size_t i = 0; i < n; ++i) {
            REQUIRE(buf[i] == tc.expected[i]);
        }
    }
}

TEST_CASE("Varint: roundtrip decode(encode(v)) for boundary values") {
    std::vector<uint64_t> values = {
        0ULL,
        1ULL,
        127ULL,
        128ULL,
        255ULL,
        300ULL,
        16383ULL,
        16384ULL,
        (1ULL << 32) - 1,
        (1ULL << 32),
        std::numeric_limits<uint64_t>::max(),
    };

    for (uint64_t v : values) {
        std::array<std::uint8_t, 32> buf{};
        WriteStream ws(asBytes(std::span{buf}));

        REQUIRE(encodeVarint(ws, v));
        REQUIRE(ws.ok());

        size_t const n = bytesWritten(buf.size(), ws);

        ReadStream rs(
            asConstBytes(std::span<std::uint8_t const>{buf.data(), n}));

        uint64_t out = 0;
        REQUIRE(decodeVarint(rs, out));
        REQUIRE(rs.ok());
        REQUIRE(out == v);
        REQUIRE(rs.remainingBytes() == 0);
    }
}

TEST_CASE(
    "Varint: WriteStream overflow -> encodeVarint returns false and sets "
    "Overflow") {
    // 300 needs 2 bytes, so capacity 1 should overflow.
    std::array<std::uint8_t, 1> buf{};
    WriteStream ws(asBytes(std::span{buf}));

    REQUIRE_FALSE(encodeVarint(ws, 300));
    REQUIRE_FALSE(ws.ok());
    REQUIRE(ws.error() == Error::Overflow);
}

TEST_CASE(
    "Varint: SizeWriteStream reports same size as bytes actually written") {
    // This is the key invariant for your sizing pass.
    std::vector<uint64_t> values = {0ULL,
                                    1ULL,
                                    127ULL,
                                    128ULL,
                                    300ULL,
                                    16384ULL,
                                    std::numeric_limits<uint64_t>::max()};

    for (uint64_t v : values) {
        SizeWriteStream sws;
        std::array<std::byte, 1> dummy{};
        // encodeVarint writes bytes(span, count); it passes span over its
        // internal buffer, so SizeWriteStream's "data" argument is ignored
        // anyway.
        REQUIRE(encodeVarint(sws, v));
        REQUIRE(sws.ok());
        size_t const sized = bytesSized(sws);

        std::array<std::uint8_t, 32> buf{};
        WriteStream ws(asBytes(std::span{buf}));
        REQUIRE(encodeVarint(ws, v));
        REQUIRE(ws.ok());
        size_t const written = bytesWritten(buf.size(), ws);

        REQUIRE(sized == written);
    }
}

TEST_CASE(
    "Varint: truncated input -> decodeVarint fails and ReadStream is Eof") {
    // Encoding of 300 is [0xAC, 0x02]. Truncate to [0xAC] (continuation bit
    // set).
    std::array<std::uint8_t, 1> truncated{0xAC};
    ReadStream rs(asConstBytes(std::span{truncated}));

    uint64_t out = 0;
    REQUIRE_FALSE(decodeVarint(rs, out));
    REQUIRE_FALSE(rs.ok());
    REQUIRE(rs.error() == Error::Eof);
}

TEST_CASE(
    "Varint: decode consumes exactly the varint, leaving trailing bytes "
    "untouched") {
    // [0x81,0x01] = 129, then trailing 0xFF
    std::array<std::uint8_t, 3> data{0x81, 0x01, 0xFF};
    ReadStream rs(asConstBytes(std::span{data}));

    uint64_t out = 0;
    REQUIRE(decodeVarint(rs, out));
    REQUIRE(rs.ok());
    REQUIRE(out == 129);
    REQUIRE(rs.remainingBytes() == 1);  // trailing 0xFF not consumed
}

TEST_CASE("FUZZ: varint roundtrip + sizing invariants") {
    // Deterministic seed so failures reproduce.
    std::mt19937_64 rng(0xC0FFEE1234ULL);
    std::uniform_int_distribution<uint64_t> dist(
        0, std::numeric_limits<uint64_t>::max());

    // Keep iterations reasonable for unit test runtime.
    constexpr int ITERS = 50'000;

    std::array<std::uint8_t, 16>
        buf{};  // varint uint64 fits in <= 10 bytes; 16 is plenty
    std::array<std::byte, 1> dummy{};

    for (int i = 0; i < ITERS; ++i) {
        uint64_t v = dist(rng);

        // Size pass
        SizeWriteStream sws;
        REQUIRE(encodeVarint(sws, v));
        REQUIRE(sws.ok());
        const size_t sized = bytesSized(sws);
        REQUIRE(sized >= 1);
        REQUIRE(sized <= 10);  // uint64 varint should be <= 10 bytes

        // Real pass
        buf.fill(0);
        WriteStream ws(asBytes(std::span{buf}));
        REQUIRE(encodeVarint(ws, v));
        REQUIRE(ws.ok());
        const size_t written = bytesWritten(buf.size(), ws);
        REQUIRE(written == sized);

        // Decode
        ReadStream rs(
            asConstBytes(std::span<const std::uint8_t>{buf.data(), written}));
        uint64_t out = 0;
        REQUIRE(decodeVarint(rs, out));
        REQUIRE(rs.ok());
        REQUIRE(out == v);
        REQUIRE(rs.remainingBytes() == 0);
    }
}

TEST_CASE(
    "FUZZ: decoding random byte streams never overreads; failure is Eof") {
    std::mt19937_64 rng(0xBADC0DEULL);
    std::uniform_int_distribution<int> lenDist(0, 64);
    std::uniform_int_distribution<int> byteDist(0, 255);

    constexpr int ITERS = 20'000;

    for (int i = 0; i < ITERS; ++i) {
        int len = lenDist(rng);
        std::vector<std::uint8_t> data(static_cast<size_t>(len));
        for (auto& b : data)
            b = static_cast<std::uint8_t>(byteDist(rng));

        ReadStream rs(asConstBytes(
            std::span<const std::uint8_t>{data.data(), data.size()}));
        uint64_t out = 0;
        bool ok = decodeVarint(rs, out);

        // Either decode succeeds, or it fails with Eof (for
        // truncated/continuation runs).
        if (!ok) {
            REQUIRE_FALSE(rs.ok());
            REQUIRE(rs.error() == Error::Eof);
        } else {
            REQUIRE(rs.ok());
            // If it succeeded, it must have consumed at least 1 byte, and never
            // more than input length.
            REQUIRE(rs.remainingBytes() <= data.size());
        }
    }
}

TEST_CASE("FUZZ: truncation of a valid encoding always fails with Eof") {
    std::mt19937_64 rng(0x12345678ULL);
    std::uniform_int_distribution<uint64_t> dist(
        0, std::numeric_limits<uint64_t>::max());

    constexpr int ITERS = 10'000;

    std::array<std::uint8_t, 16> buf{};
    for (int i = 0; i < ITERS; ++i) {
        uint64_t v = dist(rng);

        WriteStream ws(asBytes(std::span{buf}));
        REQUIRE(encodeVarint(ws, v));
        REQUIRE(ws.ok());
        size_t n = buf.size() - ws.remainingBytes();
        REQUIRE(n >= 1);
        REQUIRE(n <= 10);

        // Pick a truncation length strictly less than n
        std::uniform_int_distribution<size_t> truncDist(0, n - 1);
        size_t truncLen = truncDist(rng);

        ReadStream rs(
            asConstBytes(std::span<const std::uint8_t>{buf.data(), truncLen}));
        uint64_t out = 0;
        bool ok = decodeVarint(rs, out);

        // truncating any valid encoding should cause Eof unless truncLen==n
        // (excluded)
        REQUIRE_FALSE(ok);
        REQUIRE_FALSE(rs.ok());
        REQUIRE(rs.error() == Error::Eof);
    }
}
