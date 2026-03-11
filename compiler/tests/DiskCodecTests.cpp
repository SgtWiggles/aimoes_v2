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

// New tests: oneof, optional and arrays
TEST_CASE("Disk codec oneof round trip", "[disk][codec][oneof]") {
    std::vector<std::byte> data(1024);

    ao::schema::codec::CodecTable table;
    // Message field that contains the oneof
    table.fields.push_back(
        ao::schema::codec::CodecField{.fieldNumber = 10, .typeId = 0});

    // Define oneof arms with distinct field numbers
    table.oneofs.push_back(ao::schema::codec::CodecOneof{.fieldStart = 0,
                                                         .fieldCount = 2});
    table.oneofFieldNumbers.push_back(101);
    table.oneofFieldNumbers.push_back(102);

    ao::pack::byte::WriteStream ws{
        std::span<std::byte>(data.data(), data.size())};
    DiskEncodeCodec<ao::pack::byte::WriteStream> enc{table, ws};

    // Encode: field0 is the oneof container. Choose arm0 and write a boolean
    enc.msgBegin(0);
    enc.fieldBegin(0);
    enc.fieldId(0);
    enc.oneofEnter(0);
    auto oneofEnterBytes = ws.byteSize();
    enc.oneofArm(0, 0);  // arm0 -> field number101 gets written
    enc.boolean(true);
    enc.oneofExit();
    enc.fieldEnd();
    enc.msgEnd();

    REQUIRE(enc.ok());
    REQUIRE(ws.ok());

    ao::pack::byte::ReadStream rs{
        std::span<std::byte const>(data.data(), ws.byteSize())};
    DiskDecodeCodec<ao::pack::byte::ReadStream> dec{table, rs};

    dec.msgBegin(0);
    REQUIRE(dec.ok());
    dec.fieldBegin(0);
    REQUIRE(dec.ok());
    REQUIRE(dec.fieldId(0));
    dec.oneofEnter(0);
    REQUIRE(dec.ok());
    REQUIRE(oneofEnterBytes == rs.position());
    auto arm = dec.oneofArm(0, 1);
    REQUIRE(dec.ok());
    REQUIRE(arm == 0);
    auto val = dec.boolean();
    REQUIRE(val == true);
    dec.oneofExit();
    dec.fieldEnd();
    dec.msgEnd();

    REQUIRE(dec.ok());
    REQUIRE(rs.ok());
    REQUIRE(rs.remainingBytes() == 0);
}

TEST_CASE("Disk codec optional round trip", "[disk][codec][opt]") {
    std::vector<std::byte> data(1024);

    ao::schema::codec::CodecTable table;
    table.fields.push_back(
        ao::schema::codec::CodecField{.fieldNumber = 11, .typeId = 0});

    ao::pack::byte::WriteStream ws{
        std::span<std::byte>(data.data(), data.size())};
    DiskEncodeCodec<ao::pack::byte::WriteStream> enc{table, ws};

    // Encode optional present with a u64 value
    enc.msgBegin(0);
    enc.fieldBegin(0);
    enc.fieldId(0);
    enc.optBegin();
    enc.u64(0, 42);
    enc.optEnd();
    enc.fieldEnd();
    enc.msgEnd();

    REQUIRE(enc.ok());
    REQUIRE(ws.ok());

    ao::pack::byte::ReadStream rs{
        std::span<std::byte const>(data.data(), ws.byteSize())};
    DiskDecodeCodec<ao::pack::byte::ReadStream> dec{table, rs};

    dec.msgBegin(0);
    dec.fieldBegin(0);
    REQUIRE(dec.fieldId(0));
    dec.optBegin();
    auto v = dec.u64();
    REQUIRE(v == 42);
    dec.optEnd();
    dec.fieldEnd();
    dec.msgEnd();

    REQUIRE(dec.ok());
    REQUIRE(rs.ok());
    REQUIRE(rs.remainingBytes() == 0);
}

TEST_CASE("Disk codec array round trip", "[disk][codec][array]") {
    std::vector<std::byte> data(2048);

    ao::schema::codec::CodecTable table;
    table.fields.push_back(
        ao::schema::codec::CodecField{.fieldNumber = 12, .typeId = 0});

    ao::pack::byte::WriteStream ws{
        std::span<std::byte>(data.data(), data.size())};
    DiskEncodeCodec<ao::pack::byte::WriteStream> enc{table, ws};

    // Encode array of three u64 elements:1,2,3
    enc.msgBegin(0);
    enc.fieldBegin(0);
    enc.fieldId(0);
    enc.arrayBegin();
    // Write an explicit element type tag so decoder's arrayBegin() can read it
    {
        std::byte t = (std::byte)DiskTag::Varint;
        ws.bytes(std::span<std::byte>{&t, 1}, 1);
    }
    enc.arrayLen(0, 3);
    enc.u64(0, 1);
    enc.u64(0, 2);
    enc.u64(0, 3);
    enc.arrayEnd();
    enc.fieldEnd();
    enc.msgEnd();

    REQUIRE(enc.ok());
    REQUIRE(ws.ok());

    ao::pack::byte::ReadStream rs{
        std::span<std::byte const>(data.data(), ws.byteSize())};
    DiskDecodeCodec<ao::pack::byte::ReadStream> dec{table, rs};

    dec.msgBegin(0);
    REQUIRE(dec.ok());
    dec.fieldBegin(0);
    REQUIRE(dec.ok());
    REQUIRE(dec.fieldId(0));
    dec.arrayBegin();
    REQUIRE(dec.ok());
    auto len = dec.arrayLen(0);
    REQUIRE(len == 3);
    auto a = dec.u64();
    auto b = dec.u64();
    auto c = dec.u64();
    REQUIRE(a == 1);
    REQUIRE(b == 2);
    REQUIRE(c == 3);
    dec.arrayEnd();
    dec.fieldEnd();
    dec.msgEnd();

    REQUIRE(dec.ok());
    REQUIRE(rs.ok());
    REQUIRE(rs.remainingBytes() == 0);
}
