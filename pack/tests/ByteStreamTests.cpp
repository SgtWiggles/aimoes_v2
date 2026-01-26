#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include <ao/pack/ByteStream.h>

using namespace ao::pack;
using namespace ao::pack::byte;

static std::span<std::byte> asBytes(std::span<std::uint8_t> s) {
    return { reinterpret_cast<std::byte*>(s.data()), s.size() };
}
static std::span<std::byte const> asConstBytes(std::span<std::uint8_t const> s) {
    return { reinterpret_cast<std::byte const*>(s.data()), s.size() };
}

TEST_CASE("ReadStream: Eof when requesting beyond remaining, and error is sticky") {
    std::array<std::uint8_t, 4> raw{10,11,12,13};
    ReadStream rs(asConstBytes(std::span{raw}));

    std::span<std::byte const> view{};

    rs.bytes(view, 5);
    REQUIRE_FALSE(rs.ok());
    REQUIRE(rs.error() == Error::Eof);

    // Sticky: further ops do nothing / don't overwrite error
    const auto rem = rs.remainingBytes();
    rs.bytes(view, 1);
    REQUIRE(rs.error() == Error::Eof);
    REQUIRE(rs.remainingBytes() == rem);

    rs.require(false, Error::Overflow);
    REQUIRE(rs.error() == Error::Eof);
}

TEST_CASE("WriteStream: Overflow when writing beyond capacity, and error is sticky") {
    std::array<std::uint8_t, 4> dst{};
    WriteStream ws(asBytes(std::span{dst}));

    std::array<std::uint8_t, 5> src{1,2,3,4,5};
    ws.bytes(asBytes(std::span{src}), 5);

    REQUIRE_FALSE(ws.ok());
    REQUIRE(ws.error() == Error::Overflow);

    // Sticky
    const auto rem = ws.remainingBytes();
    std::array<std::uint8_t, 1> src2{9};
    ws.bytes(asBytes(std::span{src2}), 1);
    REQUIRE(ws.error() == Error::Overflow);
    REQUIRE(ws.remainingBytes() == rem);

    ws.require(false, Error::Eof);
    REQUIRE(ws.error() == Error::Overflow);
}

TEST_CASE("SizeWriteStream: Overflow only when size_t would overflow; error is sticky") {
    SizeWriteStream sws;
    std::array<std::byte, 1> dummy{};

    // Drive position close to SIZE_MAX: leave 2 bytes of headroom
    const size_t max = std::numeric_limits<size_t>::max();
    sws.bytes(std::span{dummy}, max - 2);
    REQUIRE(sws.ok());
    REQUIRE(sws.remainingBytes() == 2);

    sws.bytes(std::span{dummy}, 3);
    REQUIRE_FALSE(sws.ok());
    REQUIRE(sws.error() == Error::Overflow);

    // Sticky
    sws.bytes(std::span{dummy}, 1);
    REQUIRE(sws.error() == Error::Overflow);
}

TEST_CASE("WriteStream vs SizeWriteStream: same Ok/fail behavior for identical sequences when WriteStream doesn't overflow") {
    // When the real write fits, the size-pass must also be Ok and advance identically.
    std::array<std::uint8_t, 16> dst{};
    WriteStream ws(asBytes(std::span{dst}));

    SizeWriteStream sws;
    std::array<std::byte, 1> dummy{};

    // Sequence of operations (counts) that fits in dst
    constexpr size_t ops[] = {3, 0, 5, 8};

    for (size_t n : ops) {
        // For ws we need a source span; content doesn't matter for this test.
        std::array<std::uint8_t, 16> src{};
        ws.bytes(asBytes(std::span{src}), n);
        sws.bytes(std::span{dummy}, n);

        REQUIRE(ws.ok());
        REQUIRE(sws.ok());
    }

    // Additional require() checks should match too
    ws.require(true, Error::Overflow);
    sws.require(true, Error::Overflow);
    REQUIRE(ws.ok());
    REQUIRE(sws.ok());
}

TEST_CASE("WriteStream vs SizeWriteStream: require() sets identical errors and is sticky") {
    std::array<std::uint8_t, 8> dst{};
    WriteStream ws(asBytes(std::span{dst}));
    SizeWriteStream sws;

    ws.require(false, Error::Eof);
    sws.require(false, Error::Eof);

    REQUIRE_FALSE(ws.ok());
    REQUIRE_FALSE(sws.ok());
    REQUIRE(ws.error() == Error::Eof);
    REQUIRE(sws.error() == Error::Eof);

    // Sticky: further require/bytes should not overwrite
    ws.require(false, Error::Overflow);
    sws.require(false, Error::Overflow);
    REQUIRE(ws.error() == Error::Eof);
    REQUIRE(sws.error() == Error::Eof);

    std::array<std::uint8_t, 1> src{1};
    std::array<std::byte, 1> dummy{};
    ws.bytes(asBytes(std::span{src}), 1);
    sws.bytes(std::span{dummy}, 1);

    REQUIRE(ws.error() == Error::Eof);
    REQUIRE(sws.error() == Error::Eof);
}

TEST_CASE("Sizing pass matches real pass: size computed equals bytes written (no overflow case)") {
    SizeWriteStream sws;
    std::array<std::byte, 1> dummy{};

    // "serialize" with counts
    constexpr size_t ops[] = {2, 7, 1, 4};
    for (size_t n : ops) sws.bytes(std::span{dummy}, n);
    REQUIRE(sws.ok());

    const size_t sized = sws.byteSize();

    std::array<std::uint8_t, 64> dst{};
    WriteStream ws(asBytes(std::span<uint8_t>{dst}));

    std::array<std::uint8_t, 64> src{};
    for (size_t n : ops) ws.bytes(asBytes(std::span<uint8_t>{src}), n);
    REQUIRE(ws.ok());

    const size_t written = dst.size() - ws.remainingBytes();

    REQUIRE(sized == written);
}
