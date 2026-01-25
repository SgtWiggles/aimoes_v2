#include <ao/pack/BitStream.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

using namespace ao::pack::bit;

static void fillPattern(std::span<std::byte> b) {
    for (size_t i = 0; i < b.size(); ++i) {
        b[i] = std::byte(static_cast<unsigned>(0xA5u ^ (i * 17u)));
    }
}

static bool equalPrefix(std::span<std::byte const> a,
                        std::span<std::byte const> b,
                        size_t n) {
    return std::equal(a.begin(), a.begin() + static_cast<std::ptrdiff_t>(n),
                      b.begin(), b.begin() + static_cast<std::ptrdiff_t>(n));
}

static uint64_t maskN(size_t n) {
    if (n == 0)
        return 0;
    if (n >= 64)
        return ~uint64_t{0};
    return (uint64_t{1} << n) - 1;
}

TEST_CASE("WriteStream overflow on fixed buffers (bits)",
          "[WriteStream][overflow]") {
    std::array<std::byte, 1> buf{};
    fillPattern(buf);

    WriteStream ws{std::span<std::byte>(buf)};

    // 1 byte == 8 bits. Write 9 bits => overflow.
    ws.bits(0b1, 9);

    REQUIRE_FALSE(ws.ok());
    REQUIRE(ws.error() == Error::Overflow);

    // Should not exceed capacity.
    REQUIRE(ws.bitSize() <= 8);
    REQUIRE(ws.byteSize() <= 1);
}

TEST_CASE("WriteStream overflow on fixed buffers (bytes)",
          "[WriteStream][overflow]") {
    std::array<std::byte, 4> buf{};
    fillPattern(buf);

    std::array<std::byte, 5> in{};
    fillPattern(in);

    WriteStream ws{std::span<std::byte>(buf)};

    ws.align();
    ws.bytes(std::span<std::byte>(in),
             in.size());  // request 5 into 4 => overflow

    REQUIRE_FALSE(ws.ok());
    REQUIRE(ws.error() == Error::Overflow);

    REQUIRE(ws.byteSize() <= buf.size());
}

TEST_CASE("WriteStream bytes() fails when unaligned",
          "[WriteStream][unaligned]") {
    std::array<std::byte, 8> buf{};
    fillPattern(buf);

    std::array<std::byte, 2> in{};
    fillPattern(in);

    WriteStream ws{std::span<std::byte>(buf)};

    ws.bits(0b1, 1);  // now unaligned
    ws.bytes(std::span<std::byte>(in), 2);

    REQUIRE_FALSE(ws.ok());
    REQUIRE(ws.error() == Error::Unaligned);
}

TEST_CASE("ReadStream bytes() fails when unaligned",
          "[ReadStream][unaligned]") {
    std::array<std::byte, 8> data{};
    fillPattern(data);

    ReadStream rs{std::span<std::byte>(data)};

    uint64_t oneBit = 0;
    rs.bits(oneBit, 1);  // now unaligned

    std::array<std::byte, 2> out{};
    auto outSpan = std::span<std::byte>(out);
    rs.bytes(outSpan, 2);

    REQUIRE_FALSE(rs.ok());
    REQUIRE(rs.error() == Error::Unaligned);
}

TEST_CASE(
    "SizeWriteStream reports the same bit/byte sizes as WriteStream for "
    "identical operations",
    "[SizeWriteStream][WriteStream][sizes]") {
    auto const bitCount1 =
        GENERATE(size_t{0}, size_t{1}, size_t{7}, size_t{8}, size_t{9},
                 size_t{15}, size_t{16}, size_t{31});
    auto const bitCount2 =
        GENERATE(size_t{0}, size_t{3}, size_t{5}, size_t{8}, size_t{13});

    std::array<std::byte, 256> buf{};
    fillPattern(buf);

    std::array<std::byte, 32> bytesA{};
    std::array<std::byte, 17> bytesB{};
    fillPattern(bytesA);
    fillPattern(bytesB);

    WriteStream ws{std::span<std::byte>(buf)};
    SizeWriteStream ss{};

    ws.bits(0xDEADBEEF, bitCount1);
    ss.bits(0xDEADBEEF, bitCount1);

    ws.align();
    ss.align();

    ws.bytes(std::span<std::byte>(bytesA), 12);
    ss.bytes(std::span<std::byte>(bytesA), 12);

    ws.bits(0x123456789ABCDEF0ULL, bitCount2);
    ss.bits(0x123456789ABCDEF0ULL, bitCount2);

    ws.align();
    ss.align();

    ws.bytes(std::span<std::byte>(bytesB), 7);
    ss.bytes(std::span<std::byte>(bytesB), 7);

    REQUIRE(ws.ok());
    REQUIRE(ss.ok());

    REQUIRE(ws.bitSize() == ss.bitSize());
    REQUIRE(ws.byteSize() == ss.byteSize());

    // byteSize should be ceil(bitSize/8)
    REQUIRE(ws.byteSize() == (ws.bitSize() + 7) / 8);
}

TEST_CASE(
    "Round-trip: WriteStream then ReadStream yields identical bits and bytes",
    "[roundtrip]") {
    std::array<std::byte, 256> buf{};
    std::fill(buf.begin(), buf.end(), std::byte{0});

    auto const v1 = 0xF0E1D2C3B4A59687ULL;
    size_t const n1 =
        GENERATE(size_t{0}, size_t{1}, size_t{2}, size_t{7}, size_t{8},
                 size_t{9}, size_t{31}, size_t{32}, size_t{63}, size_t{64});

    uint64_t const v2 = 0x0123456789ABCDEFULL;
    size_t const n2 = GENERATE(size_t{0}, size_t{3}, size_t{5}, size_t{8},
                               size_t{13}, size_t{16});

    INFO("Generated with: " << n1 << ":" << n2);

    std::array<std::byte, 24> bytes1{};
    std::array<std::byte, 10> bytes2{};
    fillPattern(bytes1);
    fillPattern(bytes2);

    WriteStream ws{std::span<std::byte>(buf)};
    ws.bits(v1, n1);
    ws.align();
    ws.bytes(std::span<std::byte>(bytes1), bytes1.size());

    ws.bits(v2, n2);
    ws.align();
    ws.bytes(std::span<std::byte>(bytes2), bytes2.size());

    REQUIRE(ws.ok());

    size_t const writtenBytes = ws.byteSize();
    REQUIRE(writtenBytes <= buf.size());

    ReadStream rs{std::span<std::byte>(buf).first(writtenBytes)};

    uint64_t rv1 = 0, rv2 = 0;

    rs.bits(rv1, n1);
    rs.align();

    std::array<std::byte, 24> out1{};
    auto out1Span = std::span<std::byte>(out1);
    rs.bytes(out1Span, bytes1.size());
    REQUIRE(out1Span.size() == bytes1.size());
    std::copy(out1Span.begin(), out1Span.end(), out1.begin());

    rs.bits(rv2, n2);
    rs.align();

    std::array<std::byte, 10> out2{};
    auto out2Span = std::span<std::byte>(out2);
    rs.bytes(out2Span, bytes2.size());
    REQUIRE(out2Span.size() == bytes2.size());
    std::copy(out2Span.begin(), out2Span.end(), out2.begin());

    REQUIRE(rs.ok());

    REQUIRE((rv1 & maskN(n1)) == (v1 & maskN(n1)));
    REQUIRE((rv2 & maskN(n2)) == (v2 & maskN(n2)));

    REQUIRE(equalPrefix(out1, bytes1, bytes1.size()));
    REQUIRE(equalPrefix(out2, bytes2, bytes2.size()));

    REQUIRE(rs.remainingBits() == 0);
}

TEST_CASE("ReadStream detects EOF when reading past end", "[ReadStream][eof]") {
    std::array<std::byte, 1> data{};
    data[0] = std::byte{0xFF};

    ReadStream rs{std::span<std::byte>(data)};

    uint64_t out = 0;
    rs.bits(out, 8);
    REQUIRE(rs.ok());

    rs.bits(out, 1);  // past end
    REQUIRE_FALSE(rs.ok());
    REQUIRE(rs.error() == Error::Eof);
}

TEST_CASE(
    "require() fails with user-provided error on false and does not fail on "
    "true",
    "[require]") {
    std::array<std::byte, 8> buf{};
    WriteStream ws{std::span<std::byte>(buf)};

    ws.require(true, Error::BadData);
    REQUIRE(ws.ok());

    ws.require(false, Error::BadData);
    REQUIRE_FALSE(ws.ok());
    REQUIRE(ws.error() == Error::BadData);
}

TEST_CASE(
    "SizeWriteStream and WriteStream stay aligned in size reporting through "
    "align() calls",
    "[sizes][align]") {
    std::array<std::byte, 16> buf{};
    std::fill(buf.begin(), buf.end(), std::byte{0});

    WriteStream ws{std::span<std::byte>(buf)};
    SizeWriteStream ss{};

    ws.bits(0b101, 3);
    ss.bits(0b101, 3);

    // align should round up to next byte boundary, so byteSize becomes 1
    ws.align();
    ss.align();

    REQUIRE(ws.ok());
    REQUIRE(ss.ok());
    REQUIRE(ws.bitSize() == ss.bitSize());
    REQUIRE(ws.byteSize() == ss.byteSize());

    REQUIRE(ws.byteSize() == 1);
    REQUIRE(ws.bitSize() % 8 == 0);
}

TEST_CASE("WriteStream align() pads with zero bits (single partial byte)",
          "[WriteStream][align][padding]") {
    std::array<std::byte, 8> buf{};
    std::fill(buf.begin(), buf.end(), std::byte{0});  // important: known start

    WriteStream ws{std::span<std::byte>(buf)};

    // Write 3 bits: the rest of the byte should become 0 after align()
    ws.bits(0b101, 3);
    REQUIRE(ws.ok());
    REQUIRE(ws.bitSize() == 3);

    ws.align();
    REQUIRE(ws.ok());
    REQUIRE(ws.bitSize() == 8);
    REQUIRE(ws.byteSize() == 1);

    // Read back full 8 bits: top 5 padding bits must be 0
    ReadStream rs{std::span<std::byte>(buf).first(ws.byteSize())};

    uint64_t out8 = 0;
    rs.bits(out8, 8);
    REQUIRE(rs.ok());

    // The first 3 bits must match, everything else must be 0.
    REQUIRE((out8 & maskN(3)) == 0b101);
    REQUIRE((out8 >> 3) == 0);
}

TEST_CASE("WriteStream align() pads with zero bits (multiple bytes)",
          "[WriteStream][align][padding]") {
    std::array<std::byte, 16> buf{};
    std::fill(buf.begin(), buf.end(), std::byte{0});

    WriteStream ws{std::span<std::byte>(buf)};

    // Write 12 bits => 1 full byte + 4 bits into the next byte
    ws.bits(0xABC, 12);
    REQUIRE(ws.ok());
    REQUIRE(ws.bitSize() == 12);

    ws.align();
    REQUIRE(ws.ok());
    REQUIRE(ws.bitSize() == 16);
    REQUIRE(ws.byteSize() == 2);

    // Read the second byte and ensure the upper 4 bits (padding) are zero.
    ReadStream rs{std::span<std::byte>(buf).first(ws.byteSize())};

    uint64_t first12 = 0;
    rs.bits(first12, 12);
    REQUIRE(rs.ok());

    rs.align();
    REQUIRE(rs.ok());

    // After reading 12 bits, align() should skip/consume the 4 pad bits.
    // Now we should be aligned at the end of the 2-byte buffer.
    REQUIRE(rs.remainingBits() == 0);
}

TEST_CASE("WriteStream align() does not alter data when already aligned",
          "[WriteStream][align]") {
    std::array<std::byte, 8> buf{};
    std::fill(buf.begin(), buf.end(), std::byte{0});

    WriteStream ws{std::span<std::byte>(buf)};

    // Write an exactly-aligned number of bits
    ws.bits(0xAB, 8);
    REQUIRE(ws.ok());
    REQUIRE(ws.bitSize() == 8);
    REQUIRE(ws.byteSize() == 1);

    auto const before = buf[0];

    ws.align();  // should be a no-op
    REQUIRE(ws.ok());
    REQUIRE(ws.bitSize() == 8);
    REQUIRE(ws.byteSize() == 1);

    REQUIRE(buf[0] == before);
}

TEST_CASE(
    "WriteStream align() padding bits are zero even if buffer started as 0xFF",
    "[WriteStream][align][padding]") {
    std::array<std::byte, 8> buf{};
    // ensure we overwrite padding to 0
    std::fill(buf.begin(), buf.end(), std::byte{0xFF});

    WriteStream ws{std::span<std::byte>(buf)};

    ws.bits(0b1, 1);
    REQUIRE(ws.ok());
    REQUIRE(ws.bitSize() == 1);

    ws.align();
    REQUIRE(ws.ok());
    REQUIRE(ws.bitSize() == 8);
    REQUIRE(ws.byteSize() == 1);

    // Read full byte back, and check upper 7 bits are zero
    ReadStream rs{std::span<std::byte>(buf).first(1)};

    uint64_t out8 = 0;
    rs.bits(out8, 8);
    REQUIRE(rs.ok());

    REQUIRE((out8 & 0b1) == 1);
    REQUIRE((out8 >> 1) == 0);  // padding must be 0, despite initial 0xFF
}

TEST_CASE("SizeWriteStream align() increases size to next byte boundary",
          "[SizeWriteStream][align][sizes]") {
    SizeWriteStream ss{};

    ss.bits(0b101, 3);
    REQUIRE(ss.ok());
    REQUIRE(ss.bitSize() == 3);
    REQUIRE(ss.byteSize() == 1);  // ceil(3/8)

    ss.align();
    REQUIRE(ss.ok());
    REQUIRE(ss.bitSize() == 8);
    REQUIRE(ss.byteSize() == 1);

    ss.bits(0b11, 2);
    REQUIRE(ss.ok());
    REQUIRE(ss.bitSize() == 10);
    REQUIRE(ss.byteSize() == 2);

    ss.align();
    REQUIRE(ss.ok());
    REQUIRE(ss.bitSize() == 16);
    REQUIRE(ss.byteSize() == 2);
}
TEST_CASE("WriteStream: bytes() matches 8 calls to bits() per byte (LSB-first)",
          "[WriteStream][bytes][bits][lsb]") {
    auto const n = GENERATE(size_t{0}, size_t{1}, size_t{2}, size_t{7},
                            size_t{16}, size_t{33});
    INFO("Generated bytes: " << n);

    std::vector<std::byte> payload(n);
    fillPattern(payload);

    std::vector<std::byte> buf_bytes(n + 8, std::byte{0});
    std::vector<std::byte> buf_bits(n + 8, std::byte{0});

    // 1) Write using bytes()
    WriteStream ws_bytes{std::span<std::byte>(buf_bytes)};
    ws_bytes.align();
    ws_bytes.bytes(std::span<std::byte>(payload), payload.size());
    REQUIRE(ws_bytes.ok());
    REQUIRE(ws_bytes.bitSize() == payload.size() * 8);
    REQUIRE(ws_bytes.byteSize() == payload.size());

    // 2) Write using 8x bits() per byte, LSB-first
    WriteStream ws_bits{std::span<std::byte>(buf_bits)};
    for (size_t i = 0; i < payload.size(); ++i) {
        uint8_t const b = static_cast<uint8_t>(payload[i]);
        for (int bit = 0; bit < 8; ++bit) {
            uint64_t const v = (b >> bit) & 1u;  // LSB-first
            ws_bits.bits(v, 1);
        }
    }
    REQUIRE(ws_bits.ok());
    REQUIRE(ws_bits.bitSize() == payload.size() * 8);
    REQUIRE(ws_bits.byteSize() == payload.size());

    // Buffers must match exactly for the written region.
    REQUIRE(buf_bytes == buf_bits);
}

TEST_CASE("ReadStream: bytes() matches 8 calls to bits() per byte (LSB-first)",
          "[ReadStream][bytes][bits][lsb]") {
    auto const n =
        GENERATE(size_t{0}, size_t{1}, size_t{5}, size_t{16}, size_t{31});
    INFO("Byte count: " << n);

    std::vector<std::byte> data(n);
    fillPattern(data);

    // A) Read using bytes()
    std::vector<std::byte> out_bytes(n, std::byte{0});
    {
        ReadStream rs{std::span<std::byte>(data)};
        rs.align();
        auto outSpan = std::span<std::byte>{};
        rs.bytes(outSpan, n);

        REQUIRE(rs.ok());
        REQUIRE(outSpan.size() == n);
        std::copy(outSpan.begin(), outSpan.end(), out_bytes.begin());
    }

    // B) Read using bits() and reassemble bytes LSB-first
    std::vector<std::byte> out_bits(n, std::byte{0});
    {
        ReadStream rs{std::span<std::byte>(data)};
        for (size_t i = 0; i < n; ++i) {
            uint8_t b = 0;
            for (int bit = 0; bit < 8; ++bit) {
                uint64_t v = 0;
                rs.bits(v, 1);
                b |= static_cast<uint8_t>((v & 1u) << bit);  // LSB-first
            }
            out_bits[i] = std::byte{b};
        }
        REQUIRE(rs.ok());
    }

    REQUIRE(out_bits == out_bytes);
    REQUIRE(out_bytes == data);
}

TEST_CASE(
    "WriteStream sizes: bytes(N) equals bits(1) repeated 8N times (LSB-first "
    "content)",
    "[WriteStream][sizes][lsb]") {
    auto const n =
        GENERATE(size_t{0}, size_t{1}, size_t{2}, size_t{9}, size_t{32});
    INFO("Bytes generated: " << n);

    std::vector<std::byte> payload(n);
    fillPattern(payload);

    std::vector<std::byte> bufA(n + 8, std::byte{0});
    std::vector<std::byte> bufB(n + 8, std::byte{0});

    // A: bytes()
    WriteStream wsA{std::span<std::byte>(bufA)};
    wsA.align();
    wsA.bytes(std::span<std::byte>(payload), n);
    REQUIRE(wsA.ok());

    // B: 8x bits(1) per byte (LSB-first)
    WriteStream wsB{std::span<std::byte>(bufB)};
    for (size_t i = 0; i < n; ++i) {
        auto const b = static_cast<uint8_t>(payload[i]);
        for (int bit = 0; bit < 8; ++bit) {
            wsB.bits((b >> bit) & 1u, 1);
        }
    }
    REQUIRE(wsB.ok());

    REQUIRE(wsA.bitSize() == wsB.bitSize());
    REQUIRE(wsA.byteSize() == wsB.byteSize());
    REQUIRE(wsA.bitSize() == 8 * n);
    REQUIRE(wsA.byteSize() == n);

    REQUIRE(std::equal(bufA.begin(),
                       bufA.begin() + static_cast<std::ptrdiff_t>(n),
                       bufB.begin()));
}

TEST_CASE(
    "WriteStream: exact fit using bytes() succeeds and next write overflows",
    "[WriteStream][exact][bytes]") {
    const auto n = GENERATE(size_t{0}, size_t{1}, size_t{2}, size_t{7},
                            size_t{16}, size_t{63});

    std::vector<std::byte> buf(n, std::byte{0});
    std::vector<std::byte> payload(n);
    fillPattern(payload);

    WriteStream ws{std::span<std::byte>(buf)};

    ws.align();
    ws.bytes(std::span<std::byte>(payload), n);

    REQUIRE(ws.ok());
    REQUIRE(ws.byteSize() == n);
    REQUIRE(ws.bitSize() == 8 * n);

    // Any additional write should overflow (even 1 bit), since the buffer is
    // exactly full.
    ws.bits(1, 1);
    REQUIRE_FALSE(ws.ok());
    REQUIRE(ws.error() == Error::Overflow);

    // Content should match exactly
    REQUIRE(std::equal(buf.begin(), buf.end(), payload.begin()));
}

TEST_CASE(
    "WriteStream: exact fit using bits() succeeds and next bit write overflows",
    "[WriteStream][exact][bits]") {
    const auto bytesN =
        GENERATE(size_t{0}, size_t{1}, size_t{2}, size_t{9}, size_t{32});

    std::vector<std::byte> buf(bytesN, std::byte{0});
    std::vector<std::byte> payload(bytesN);
    fillPattern(payload);

    WriteStream ws{std::span<std::byte>(buf)};

    // Write exactly 8*bytesN bits, LSB-first per byte.
    for (size_t i = 0; i < bytesN; ++i) {
        const uint8_t b = static_cast<uint8_t>(payload[i]);
        for (int bit = 0; bit < 8; ++bit) {
            ws.bits((b >> bit) & 1u, 1);
        }
    }

    REQUIRE(ws.ok());
    REQUIRE(ws.byteSize() == bytesN);
    REQUIRE(ws.bitSize() == 8 * bytesN);

    // One more bit should overflow
    ws.bits(0, 1);
    REQUIRE_FALSE(ws.ok());
    REQUIRE(ws.error() == Error::Overflow);

    REQUIRE(std::equal(buf.begin(), buf.end(), payload.begin()));
}

TEST_CASE(
    "WriteStream: exact fit using mixed bits+bytes consumes buffer exactly",
    "[WriteStream][exact][mixed]") {
    // Choose a total size, then split into (headBits) + (middleBytes) +
    // (tailBits) that still exactly fills.
    const auto totalBytes =
        GENERATE(size_t{1}, size_t{2}, size_t{4}, size_t{8});
    const auto headBits = GENERATE(size_t{0}, size_t{1}, size_t{3}, size_t{7});

    // Ensure there's room: headBits occupies ceil(headBits/8) bytes, but we
    // will align before middleBytes, so headBits must be < 8 to keep it to at
    // most 1 byte.
    REQUIRE(headBits < 8);

    std::vector<std::byte> buf(totalBytes, std::byte{0});

    // We'll fill:
    // - headBits bits
    // - align() to next byte
    // - some whole bytes
    // - tail bits that finish exactly at end (then align optional)
    //
    // Compute how many full bytes are available after headBits + align.
    const size_t headBytes = (headBits == 0) ? 0 : 1;  // because headBits < 8
    const size_t afterAlignByteIndex =
        headBytes;  // align moves us to next byte if unaligned
    const size_t remainingBytesAfterAlign = totalBytes - afterAlignByteIndex;

    // Pick middleBytes between 0..remainingBytesAfterAlign
    const auto middleBytes = GENERATE_COPY(size_t{0}, size_t{1}, size_t{2});
    const size_t mid = std::min(middleBytes, remainingBytesAfterAlign);

    const size_t bytesUsedSoFar = afterAlignByteIndex + mid;
    const size_t bytesLeft = totalBytes - bytesUsedSoFar;

    // Tail bits must be exactly bytesLeft*8 bits (so we end exactly at the
    // end).
    const size_t tailBits = bytesLeft * 8;

    std::vector<std::byte> middlePayload(mid);
    fillPattern(middlePayload);

    WriteStream ws{std::span<std::byte>(buf)};

    // head bits: write an easy-to-check pattern, LSB-first
    if (headBits > 0) {
        ws.bits(0b01010101u, headBits);
    }
    ws.align();

    if (mid > 0) {
        ws.bytes(std::span<std::byte>(middlePayload), mid);
    }

    // tail bits: write zero bits (doesn't matter, but deterministic)
    if (tailBits > 0) {
        // Write tailBits zeros in chunks for speed/readability.
        size_t remaining = tailBits;
        while (remaining > 0) {
            const size_t chunk = std::min<size_t>(remaining, 64);
            ws.bits(0, chunk);
            remaining -= chunk;
        }
    }

    REQUIRE(ws.ok());
    REQUIRE(ws.byteSize() == totalBytes);
    REQUIRE(ws.bitSize() == 8 * totalBytes);

    // Exactly full: any additional byte write should overflow.
    std::array<std::byte, 1> one{std::byte{0x11}};
    ws.align();  // should already be aligned
    ws.bytes(std::span<std::byte>(one), 1);
    REQUIRE_FALSE(ws.ok());
    REQUIRE(ws.error() == Error::Overflow);

    // Middle bytes should appear at the aligned offset.
    if (mid > 0) {
        REQUIRE(std::equal(
            buf.begin() + static_cast<std::ptrdiff_t>(afterAlignByteIndex),
            buf.begin() +
                static_cast<std::ptrdiff_t>(afterAlignByteIndex + mid),
            middlePayload.begin()));
    }
}

TEST_CASE(
    "WriteStream: exact fit using bits(count) in larger chunks succeeds, then "
    "overflows on next write",
    "[WriteStream][exact][bits][chunks]") {
    const auto bytesN = GENERATE(size_t{1}, size_t{2}, size_t{8});

    std::vector<std::byte> buf(bytesN, std::byte{0});
    WriteStream ws{std::span<std::byte>(buf)};

    // Fill exactly bytesN*8 bits using varying chunk sizes that sum exactly.
    size_t remaining = bytesN * 8;
    while (remaining > 0) {
        const size_t chunk =
            (remaining >= 13) ? 13 : remaining;  // odd chunk size
        ws.bits(~uint64_t{0},
                chunk);  // write 1s; value masking is implementation-defined
                         // but should be fine for size
        remaining -= chunk;
    }

    REQUIRE(ws.ok());
    REQUIRE(ws.bitSize() == bytesN * 8);
    REQUIRE(ws.byteSize() == bytesN);

    ws.bits(1, 1);
    REQUIRE_FALSE(ws.ok());
    REQUIRE(ws.error() == Error::Overflow);
}
