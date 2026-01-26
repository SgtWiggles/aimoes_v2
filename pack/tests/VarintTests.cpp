#include <catch2/catch_all.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <span>
#include <vector>

#include <ao/pack/ByteStream.h>
#include <ao/pack/VarInt.h>

using namespace ao::pack;
using namespace ao::pack::byte;

static std::span<std::byte> asBytes(std::span<std::uint8_t> s) {
    return {reinterpret_cast<std::byte*>(s.data()), s.size()};
}
static std::span<std::byte const> asConstBytes(
    std::span<std::uint8_t const> s) {
    return {reinterpret_cast<std::byte const*>(s.data()), s.size()};
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

        size_t const n = ws.byteSize();
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

        size_t const n = ws.byteSize();

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
        size_t const sized = sws.byteSize();

        std::array<std::uint8_t, 32> buf{};
        WriteStream ws(asBytes(std::span{buf}));
        REQUIRE(encodeVarint(ws, v));
        REQUIRE(ws.ok());
        size_t const written = ws.byteSize();

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
        const size_t sized = sws.byteSize();
        REQUIRE(sized >= 1);
        REQUIRE(sized <= 10);  // uint64 varint should be <= 10 bytes

        // Real pass
        buf.fill(0);
        WriteStream ws(asBytes(std::span{buf}));
        REQUIRE(encodeVarint(ws, v));
        REQUIRE(ws.ok());
        const size_t written = ws.byteSize();
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
            INFO("Error was: " << (int)rs.error());
            auto passed =
                rs.error() == Error::Eof || rs.error() == Error::BadData;
            REQUIRE(passed);
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

TEST_CASE("PrefixInt: Header for boundary values") {
    for (size_t i = 0; i < 127; ++i) {
        auto [v, vShift, vBytes] = encodePrefixIntHeader(i);
        INFO("Current number: " << i);
        REQUIRE((v & 0x80) == 0);
        REQUIRE((v & 0x7F) == i);

        auto [bytes, shift, lower] = decodePrefixIntHeader(v);
        REQUIRE(bytes == 0);
        REQUIRE(lower == (i & 0x7F));
        REQUIRE(shift == 7);

        REQUIRE((int)vBytes == (int)bytes);
    }

    for (size_t i = 128; i < 16384; ++i) {
        auto [v, vShift, vBytes] = encodePrefixIntHeader(i);
        INFO("Current number: " << i);
        REQUIRE((v & 0xC0) == 0x80);
        REQUIRE((v & 0x3F) == (i & 0x3F));

        auto [bytes, shift, lower] = decodePrefixIntHeader(v);
        REQUIRE(bytes == 1);
        REQUIRE(lower == (i & 0x3F));
        REQUIRE(shift == 6);

        REQUIRE((int)vBytes == (int)bytes);
    }

    for (size_t i = 0; i < 128; ++i) {
        auto n = 2097152 - i - 1;
        auto [v, vShift, vBytes] = encodePrefixIntHeader(n);

        INFO("Current number: " << n);
        REQUIRE((v & 0xE0) == 0xC0);
        REQUIRE((v & 0x1F) == (n & 0x1F));

        auto [bytes, shift, lower] = decodePrefixIntHeader(v);
        REQUIRE(bytes == 2);
        REQUIRE(lower == (n & 0x1F));
        REQUIRE(shift == 5);

        REQUIRE((int)vBytes == (int)bytes);
    }

    for (size_t i = 0; i < 128; ++i) {
        auto n = 2097152 + i;
        auto [v, vShift, vBytes] = encodePrefixIntHeader(n);

        INFO("Current number: " << n);
        REQUIRE((v & 0xF0) == 0xE0);
        REQUIRE((v & 0x0F) == (n & 0x0F));

        auto [bytes, shift, lower] = decodePrefixIntHeader(v);
        REQUIRE(bytes == 3);
        REQUIRE(lower == (n & 0x0F));
        REQUIRE(shift == 4);

        REQUIRE((int)vBytes == (int)bytes);
    }

    for (size_t i = 0; i < 128; ++i) {
        auto n = (0x1ull << 56) - i - 1;
        auto [v, vShift, vBytes] = encodePrefixIntHeader(n);

        INFO("Current number: " << n);
        REQUIRE((v & 0xFF) == 0xFE);

        auto [bytes, shift, lower] = decodePrefixIntHeader(v);
        REQUIRE(bytes == 7);
        REQUIRE(lower == 0);
        REQUIRE(shift == 0);

        REQUIRE((int)vBytes == (int)bytes);
    }

    for (size_t i = 0; i < 128; ++i) {
        auto n = std::numeric_limits<uint64_t>::max() - i;
        auto [v, vShift, vBytes] = encodePrefixIntHeader(n);

        INFO("Current number: " << n);
        REQUIRE((v & 0xFF) == 0xFF);

        auto [bytes, shift, lower] = decodePrefixIntHeader(v);
        REQUIRE(bytes == 8);
        REQUIRE(lower == 0);
        REQUIRE(shift == 0);

        REQUIRE((int)vBytes == (int)bytes);
    }
}

TEST_CASE("PrefixInt: roundtrip decode(encode(v)) for boundary values") {
    std::vector<uint64_t> values = {
        0ULL,
        1ULL,
        2ULL,
        127ULL,
        128ULL,
        255ULL,
        256ULL,
        16383ULL,
        16384ULL,
        (1ULL << 32) - 1,
        (1ULL << 32),
        std::numeric_limits<uint64_t>::max() - 1,
        std::numeric_limits<uint64_t>::max(),
    };

    for (uint64_t v : values) {
        std::array<std::uint8_t, 16>
            buf{};  // should be plenty for this scheme; adjust if needed
        INFO("Current value: " << v);

        WriteStream ws(asBytes(std::span{buf}));

        auto ok = encodePrefixInt(ws, v);
        REQUIRE(ok);
        REQUIRE(ws.ok());

        const size_t n = ws.byteSize();
        REQUIRE(n >= 1);
        REQUIRE(n <= buf.size());

        ReadStream rs(asConstBytes(std::span<uint8_t const>{buf.data(), n}));

        uint64_t out = 0;
        ok = decodePrefixInt(rs, out);
        REQUIRE(ok);
        REQUIRE(rs.ok());
        REQUIRE(out == v);
        REQUIRE(rs.remainingBytes() == 0);
    }
}

TEST_CASE("PrefixInt: SizeWriteStream matches bytes written by WriteStream") {
    std::vector<uint64_t> values = {
        0ULL,     1ULL,         127ULL,
        128ULL,   255ULL,       256ULL,
        16384ULL, (1ULL << 40), std::numeric_limits<uint64_t>::max()};

    for (uint64_t v : values) {
        SizeWriteStream sws;
        REQUIRE(encodePrefixInt(sws, v));
        REQUIRE(sws.ok());
        const size_t sized = sws.byteSize();
        REQUIRE(sized >= 1);
        REQUIRE(sized <= 16);  // sizing sanity check for test buffer

        std::array<std::uint8_t, 32> buf{};
        WriteStream ws(asBytes(std::span{buf}));
        REQUIRE(encodePrefixInt(ws, v));
        REQUIRE(ws.ok());
        const size_t written = ws.byteSize();

        REQUIRE(sized == written);
    }
}

TEST_CASE(
    "PrefixInt: WriteStream overflow -> encodePrefixInt returns false and sets "
    "Overflow") {
    // We don't need to know exact encoded length: find it via sizing, then
    // force a too-small buffer.
    uint64_t v = std::numeric_limits<uint64_t>::max();

    SizeWriteStream sws;
    REQUIRE(encodePrefixInt(sws, v));
    REQUIRE(sws.ok());
    const size_t needed = sws.byteSize();
    REQUIRE(needed >= 1);

    // Buffer smaller than needed must overflow.
    std::vector<std::uint8_t> small(needed - 1, 0);
    WriteStream ws(asBytes(std::span{small}));

    REQUIRE_FALSE(encodePrefixInt(ws, v));
    REQUIRE_FALSE(ws.ok());
    REQUIRE(ws.error() == Error::Overflow);
}

TEST_CASE("PrefixInt: truncated input -> decodePrefixInt fails with EoF") {
    // Encode something that requires multiple bytes, then truncate in all
    // possible ways.
    uint64_t v = (1ULL << 56) +
                 123;  // likely to require more bytes than the header-only case

    std::array<std::uint8_t, 32> buf{};
    WriteStream ws(asBytes(std::span{buf}));
    REQUIRE(encodePrefixInt(ws, v));
    REQUIRE(ws.ok());
    const size_t n = ws.byteSize();
    REQUIRE(n >= 2);

    for (size_t trunc = 0; trunc < n; ++trunc) {
        ReadStream rs(
            asConstBytes(std::span<const std::uint8_t>{buf.data(), trunc}));
        uint64_t out = 0;

        bool ok = decodePrefixInt(rs, out);
        REQUIRE_FALSE(ok);
        REQUIRE_FALSE(rs.ok());
        REQUIRE(rs.error() == Error::Eof);
    }
}

TEST_CASE(
    "PrefixInt: decode consumes exactly one encoded integer and leaves "
    "trailing bytes") {
    uint64_t v = 300;

    std::array<std::uint8_t, 64> buf{};
    WriteStream ws(asBytes(std::span{buf}));
    REQUIRE(encodePrefixInt(ws, v));
    REQUIRE(ws.ok());
    const size_t n = ws.byteSize();

    // Append a known trailing marker
    buf[n] = 0xEE;
    buf[n + 1] = 0xFF;

    ReadStream rs(
        asConstBytes(std::span<const std::uint8_t>{buf.data(), n + 2}));

    uint64_t out = 0;
    REQUIRE(decodePrefixInt(rs, out));
    REQUIRE(rs.ok());
    REQUIRE(out == v);
    REQUIRE(rs.remainingBytes() == 2);
}

TEST_CASE(
    "PrefixInt: header width stays within 0..8 (sanity) and decoding matches") {
    // This test is more about catching bugs like reading wrong span types /
    // wrong counts.
    std::vector<uint64_t> values = {0ULL,         127ULL,      128ULL,
                                    16383ULL,     16384ULL,    (1ULL << 20),
                                    (1ULL << 35), (1ULL << 63)};

    for (uint64_t v : values) {
        std::array<std::uint8_t, 32> buf{};
        WriteStream ws(asBytes(std::span{buf}));
        REQUIRE(encodePrefixInt(ws, v));
        REQUIRE(ws.ok());
        const size_t n = ws.byteSize();

        // headerWidth is countl_one(header byte) from the decoder's
        // perspective; must be <= 8 always.
        const std::byte header = static_cast<std::byte>(buf[0]);
        const unsigned hw = std::countl_one(static_cast<unsigned char>(header));
        REQUIRE(hw <= 8);

        ReadStream rs(
            asConstBytes(std::span<const std::uint8_t>{buf.data(), n}));
        uint64_t out = 0;
        REQUIRE(decodePrefixInt(rs, out));
        REQUIRE(out == v);
    }
}

TEST_CASE("FUZZ: PrefixInt roundtrip + sizing invariants") {
    std::mt19937_64 rng(0xA11CE5EEDULL);
    std::uniform_int_distribution<uint64_t> dist(
        0, std::numeric_limits<uint64_t>::max());

    constexpr int ITERS = 50'000;

    std::array<std::uint8_t, 32> buf{};

    for (int i = 0; i < ITERS; ++i) {
        uint64_t v = dist(rng);

        // Sizing pass must succeed (it only overflows on size_t overflow).
        SizeWriteStream sws;
        REQUIRE(encodePrefixInt(sws, v));
        REQUIRE(sws.ok());
        const size_t sized = sws.byteSize();
        REQUIRE(sized >= 1);
        REQUIRE(sized <= buf.size());  // sanity for this test buffer

        // Real encode must write exactly `sized` bytes.
        buf.fill(0);
        WriteStream ws(asBytes(std::span{buf}));
        REQUIRE(encodePrefixInt(ws, v));
        REQUIRE(ws.ok());
        const size_t written = ws.byteSize();
        REQUIRE(written == sized);

        // Decode must recover original and consume exactly `written` bytes.
        ReadStream rs(
            asConstBytes(std::span<const std::uint8_t>{buf.data(), written}));
        uint64_t out = 0;
        REQUIRE(decodePrefixInt(rs, out));
        REQUIRE(rs.ok());
        REQUIRE(out == v);
        REQUIRE(rs.remainingBytes() == 0);
    }
}

TEST_CASE(
    "FUZZ: PrefixInt decoding random byte streams never overreads; failure is "
    "EoF") {
    std::mt19937_64 rng(0xD00DFEEDULL);
    std::uniform_int_distribution<int> lenDist(0, 128);
    std::uniform_int_distribution<int> byteDist(0, 255);

    constexpr int ITERS = 30'000;

    for (int i = 0; i < ITERS; ++i) {
        const int len = lenDist(rng);
        std::vector<std::uint8_t> data(static_cast<size_t>(len));
        for (auto& b : data)
            b = static_cast<std::uint8_t>(byteDist(rng));

        ReadStream rs(asConstBytes(
            std::span<const std::uint8_t>{data.data(), data.size()}));
        uint64_t out = 0;
        bool ok = decodePrefixInt(rs, out);

        if (!ok) {
            REQUIRE_FALSE(rs.ok());
            REQUIRE(rs.error() == Error::Eof);
        } else {
            REQUIRE(rs.ok());
            REQUIRE(rs.remainingBytes() <= data.size());
        }
    }
}

TEST_CASE("FUZZ: truncation of a valid encoding always fails with EoF") {
    std::mt19937_64 rng(0x1234BEEFCAFELL);
    std::uniform_int_distribution<uint64_t> dist(
        0, std::numeric_limits<uint64_t>::max());

    constexpr int ITERS = 10'000;
    std::array<std::uint8_t, 64> buf{};

    for (int i = 0; i < ITERS; ++i) {
        uint64_t v = dist(rng);

        WriteStream ws(asBytes(std::span{buf}));
        REQUIRE(encodePrefixInt(ws, v));
        REQUIRE(ws.ok());
        const size_t n = ws.byteSize();
        REQUIRE(n >= 1);
        REQUIRE(n <= buf.size());

        // Truncate to any length smaller than n
        if (n == 1)
            continue;  // can't truncate to a smaller non-negative length that
                       // tests anything interesting

        std::uniform_int_distribution<size_t> truncDist(0, n - 1);
        const size_t truncLen = truncDist(rng);

        ReadStream rs(
            asConstBytes(std::span<const std::uint8_t>{buf.data(), truncLen}));
        uint64_t out = 0;
        bool ok = decodePrefixInt(rs, out);

        REQUIRE_FALSE(ok);
        REQUIRE_FALSE(rs.ok());
        REQUIRE(rs.error() == Error::Eof);
    }
}

TEST_CASE("FUZZ: WriteStream overflow boundary using sizing oracle") {
    std::mt19937_64 rng(0xFACE0FFULL);
    std::uniform_int_distribution<uint64_t> dist(
        0, std::numeric_limits<uint64_t>::max());

    constexpr int ITERS = 10'000;

    for (int i = 0; i < ITERS; ++i) {
        uint64_t v = dist(rng);

        SizeWriteStream sws;
        REQUIRE(encodePrefixInt(sws, v));
        REQUIRE(sws.ok());
        size_t const needed = sws.byteSize();
        REQUIRE(needed >= 1);
        REQUIRE(needed <= 64);  // sanity guard

        if (needed == 1)
            continue;  // can't make "needed-1" underflow

        std::vector<std::uint8_t> small(needed - 1, 0);
        WriteStream ws(asBytes(std::span{small}));
        bool ok = encodePrefixInt(ws, v);

        REQUIRE_FALSE(ok);
        REQUIRE_FALSE(ws.ok());
        REQUIRE(ws.error() == Error::Overflow);
    }
}
