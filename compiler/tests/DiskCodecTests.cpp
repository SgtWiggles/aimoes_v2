#include <catch2/catch_all.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "ao/pack/ByteStream.h"
#include "ao/pack/Varint.h"
#include "ao/schema/Codec.h"
#include "ao/schema/DiskCodec.h"

using namespace ao::schema::codec::disk;

TEST_CASE("Disk codec round trip basic fields", "[disk][codec]") {
    std::vector<std::byte> data(1024);

    // Build a minimal codec table with two fields
    ao::schema::codec::CodecTable table;
    table.fields.push_back(
        ao::schema::codec::CodecField{.fieldNumber = 10, .typeId = 0});
    table.fields.push_back(
        ao::schema::codec::CodecField{.fieldNumber = 20, .typeId = 0});

    ao::pack::byte::WriteStream ws{
        std::span<std::byte>(data.data(), data.size())};
    DiskEncodeCodec<ao::pack::byte::WriteStream> enc{table, ws};

    // Encode message with two fields
    enc.msgBegin(0);

    enc.fieldBegin(0);
    enc.fieldId(0);
    enc.u64(0, 123456789);
    enc.fieldEnd();

    enc.fieldBegin(1);
    enc.fieldId(1);
    enc.boolean(true);
    enc.fieldEnd();

    enc.msgEnd();

    REQUIRE(enc.ok());
    REQUIRE(ws.ok());

    // Decode
    ao::pack::byte::ReadStream rs{
        std::span<std::byte const>(data.data(), ws.byteSize())};
    DiskDecodeCodec<ao::pack::byte::ReadStream> dec{table, rs};

    dec.msgBegin(0);

    dec.fieldBegin(0);
    auto success = dec.fieldId(0);
    REQUIRE(success);
    auto v = dec.u64();
    REQUIRE(v == 123456789);
    dec.fieldEnd();

    dec.fieldBegin(1);
    REQUIRE(dec.fieldId(1));
    auto b = dec.boolean();
    REQUIRE(b == true);
    dec.fieldEnd();

    dec.msgEnd();

    REQUIRE(dec.ok());
    REQUIRE(rs.ok());
    REQUIRE(rs.remainingBytes() == 0);
}

TEST_CASE("Disk codec skip field works", "[disk][codec][skip]") {
    std::vector<std::byte> data(1024);

    ao::schema::codec::CodecTable table;
    table.fields.push_back(
        ao::schema::codec::CodecField{.fieldNumber = 1, .typeId = 0});
    table.fields.push_back(
        ao::schema::codec::CodecField{.fieldNumber = 2, .typeId = 0});

    ao::pack::byte::WriteStream ws{
        std::span<std::byte>(data.data(), data.size())};
    DiskEncodeCodec<ao::pack::byte::WriteStream> enc{table, ws};

    // Encode a message where first field is large varint and second is small
    enc.msgBegin(0);

    enc.fieldBegin(0);
    enc.fieldId(0);
    enc.u64(0, 0xFFFFFFFFull);
    enc.fieldEnd();

    enc.fieldBegin(1);
    enc.fieldId(1);
    enc.u64(0, 99);
    enc.fieldEnd();

    enc.msgEnd();

    REQUIRE(enc.ok());
    REQUIRE(ws.ok());

    ao::pack::byte::ReadStream rs{
        std::span<std::byte const>(data.data(), ws.byteSize())};
    DiskDecodeCodec<ao::pack::byte::ReadStream> dec{table, rs};

    dec.msgBegin(0);

    // Start first field and skip it
    dec.fieldBegin(0);
    // skipField is expected to skip the value and consume the End tag
    REQUIRE(dec.fieldId(0));
    REQUIRE(dec.skipField(0));
    REQUIRE(dec.ok());

    // Now next field should be readable
    dec.fieldBegin(1);
    REQUIRE(dec.fieldId(1));
    auto v = dec.u64();
    REQUIRE(v == 99);
    dec.fieldEnd();

    dec.msgEnd();

    REQUIRE(dec.ok());
    REQUIRE(rs.ok());
    REQUIRE(rs.remainingBytes() == 0);
}
