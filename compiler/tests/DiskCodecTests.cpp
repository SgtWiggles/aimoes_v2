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

// Oneof message arm test
TEST_CASE("Disk codec oneof message arm", "[disk][codec][oneof][message]") {
    std::vector<std::byte> data(1024);

    ao::schema::codec::CodecTable table;
    // field0 is the oneof container
    table.fields.push_back(
        ao::schema::codec::CodecField{.fieldNumber = 10, .typeId = 0});
    // field1 is a nested message's field
    table.fields.push_back(
        ao::schema::codec::CodecField{.fieldNumber = 201, .typeId = 0});

    // oneof with single arm that maps to field number201
    table.oneofs.push_back(ao::schema::codec::CodecOneof{.fieldStart = 0,
                                                         .fieldCount = 1});
    table.oneofFieldNumbers.push_back(201);

    ao::pack::byte::WriteStream ws{
        std::span<std::byte>(data.data(), data.size())};
    DiskEncodeCodec<ao::pack::byte::WriteStream> enc{table, ws};

    // Encode: oneof container field with a message arm containing one u64
    enc.msgBegin(0);
    enc.fieldBegin(0);
    enc.fieldId(0);
    enc.oneofEnter(0);
    enc.oneofArm(0, 0);  // writes arm field number (201)
    // Now write the nested message
    enc.msgBegin(0);
    enc.fieldBegin(1);
    enc.fieldId(1);
    enc.u64(0, 55);
    enc.fieldEnd();
    enc.msgEnd();
    enc.oneofExit();
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
    dec.oneofEnter(0);
    auto arm = dec.oneofArm(0, 1);
    REQUIRE(arm == 0);
    // Next should be nested message
    dec.msgBegin(0);
    dec.fieldBegin(1);
    REQUIRE(dec.fieldId(1));
    auto nv = dec.u64();
    REQUIRE(nv == 55);
    dec.fieldEnd();
    dec.msgEnd();
    dec.oneofExit();
    dec.fieldEnd();
    dec.msgEnd();

    REQUIRE(dec.ok());
    REQUIRE(rs.ok());
    REQUIRE(rs.remainingBytes() == 0);
}

// Arrays of messages
TEST_CASE("Disk codec array of messages", "[disk][codec][array][message]") {
    std::vector<std::byte> data(2048);

    ao::schema::codec::CodecTable table;
    // field0 is the array field in the outer message
    table.fields.push_back(
        ao::schema::codec::CodecField{.fieldNumber = 30, .typeId = 0});
    // field1 is the nested message's single field
    table.fields.push_back(
        ao::schema::codec::CodecField{.fieldNumber = 41, .typeId = 0});

    ao::pack::byte::WriteStream ws{
        std::span<std::byte>(data.data(), data.size())};
    DiskEncodeCodec<ao::pack::byte::WriteStream> enc{table, ws};

    enc.msgBegin(0);
    enc.fieldBegin(0);
    enc.fieldId(0);
    enc.arrayBegin();
    // element type tag (messages)
    {
        std::byte t = (std::byte)DiskTag::MsgBegin;
        ws.bytes(std::span<std::byte>{&t, 1}, 1);
    }
    enc.arrayLen(0, 2);
    // element1
    enc.msgBegin(0);
    enc.fieldBegin(1);
    enc.fieldId(1);
    enc.u64(0, 7);
    enc.fieldEnd();
    enc.msgEnd();
    // element2
    enc.msgBegin(0);
    enc.fieldBegin(1);
    enc.fieldId(1);
    enc.u64(0, 8);
    enc.fieldEnd();
    enc.msgEnd();
    enc.arrayEnd();
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
    dec.arrayBegin();
    auto len = dec.arrayLen(0);
    REQUIRE(len == 2);
    // element1
    dec.msgBegin(0);
    dec.fieldBegin(1);
    REQUIRE(dec.fieldId(1));
    auto e1 = dec.u64();
    REQUIRE(e1 == 7);
    dec.fieldEnd();
    dec.msgEnd();
    // element2
    dec.msgBegin(0);
    dec.fieldBegin(1);
    REQUIRE(dec.fieldId(1));
    auto e2 = dec.u64();
    REQUIRE(e2 == 8);
    dec.fieldEnd();
    dec.msgEnd();
    dec.arrayEnd();
    dec.fieldEnd();
    dec.msgEnd();

    REQUIRE(dec.ok());
    REQUIRE(rs.ok());
    REQUIRE(rs.remainingBytes() == 0);
}

// Message that contains an array field
TEST_CASE("Disk codec message containing array field", "[disk][codec][message][array]") {
    std::vector<std::byte> data(2048);

    ao::schema::codec::CodecTable table;
    // field0 is the nested message field in the outer message
    table.fields.push_back(
        ao::schema::codec::CodecField{.fieldNumber = 50, .typeId = 0});
    // field1 is the array field inside the nested message
    table.fields.push_back(
        ao::schema::codec::CodecField{.fieldNumber = 60, .typeId = 0});

    ao::pack::byte::WriteStream ws{
        std::span<std::byte>(data.data(), data.size())};
    DiskEncodeCodec<ao::pack::byte::WriteStream> enc{table, ws};

    enc.msgBegin(0);
    enc.fieldBegin(0);
    enc.fieldId(0);
    // begin nested message
    enc.msgBegin(0);
    // nested message's array field
    enc.fieldBegin(1);
    enc.fieldId(1);
    enc.arrayBegin();
    // element type tag (varint)
    {
        std::byte t = (std::byte)DiskTag::Varint;
        ws.bytes(std::span<std::byte>{&t, 1}, 1);
    }
    enc.arrayLen(0, 3);
    enc.u64(0, 11);
    enc.u64(0, 12);
    enc.u64(0, 13);
    enc.arrayEnd();
    enc.fieldEnd();
    enc.msgEnd();
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
    dec.msgBegin(0);
    dec.fieldBegin(1);
    REQUIRE(dec.fieldId(1));
    dec.arrayBegin();
    auto len = dec.arrayLen(0);
    REQUIRE(len == 3);
    auto a1 = dec.u64();
    auto a2 = dec.u64();
    auto a3 = dec.u64();
    REQUIRE(a1 == 11);
    REQUIRE(a2 == 12);
    REQUIRE(a3 == 13);
    dec.arrayEnd();
    dec.fieldEnd();
    dec.msgEnd();
    dec.fieldEnd();
    dec.msgEnd();

    REQUIRE(dec.ok());
    REQUIRE(rs.ok());
    REQUIRE(rs.remainingBytes() == 0);
}

// Malformed inputs ---------------------------------------------------------

TEST_CASE("Disk codec truncated varint yields Eof error", "[disk][codec][malformed]") {
    std::vector<std::byte> data(1024);
    ao::schema::codec::CodecTable table;
    table.fields.push_back({.fieldNumber = 1, .typeId = 0});

    ao::pack::byte::WriteStream ws{std::span<std::byte>(data.data(), data.size())};
    DiskEncodeCodec<ao::pack::byte::WriteStream> enc{table, ws};

    // Encode a large varint so it occupies multiple bytes
    enc.msgBegin(0);
    enc.fieldBegin(0);
    enc.fieldId(0);
    enc.u64(0, (1ull << 56));
    enc.fieldEnd();
    enc.msgEnd();

    REQUIRE(enc.ok());
    REQUIRE(ws.ok());

    // Truncate the last byte to simulate EOF in the middle of varint
    auto fullSize = ws.byteSize();
    REQUIRE(fullSize > 3);
    ao::pack::byte::ReadStream rs{std::span<std::byte const>(data.data(), fullSize - 3)};
    DiskDecodeCodec<ao::pack::byte::ReadStream> dec{table, rs};

    dec.msgBegin(0);
    dec.fieldBegin(0);
    REQUIRE(dec.fieldId(0));
    // Attempt to read varint: should fail due to truncated data
    (void)dec.u64();
    REQUIRE_FALSE(dec.ok());
    REQUIRE(dec.error() == ao::pack::Error::Eof);
}

TEST_CASE("Disk codec invalid top-level tag yields BadData", "[disk][codec][malformed]") {
    std::vector<std::byte> data(16);
    ao::pack::byte::WriteStream ws{std::span<std::byte>(data.data(), data.size())};

    // Write an invalid tag value as a prefix-int (large number)
    ao::pack::encodePrefixInt(ws, 255);

    ao::pack::byte::ReadStream rs{std::span<std::byte const>(data.data(), ws.byteSize())};
    ao::schema::codec::CodecTable table;
    DiskDecodeCodec<ao::pack::byte::ReadStream> dec{table, rs};

    // Expect reading a MsgBegin to fail due to bad tag
    dec.msgBegin(0);
    REQUIRE_FALSE(dec.ok());
    REQUIRE(dec.error() == ao::pack::Error::BadData);
}

TEST_CASE("Disk codec unknown oneof arm returns max without error", "[disk][codec][malformed]") {
    std::vector<std::byte> data(256);
    ao::schema::codec::CodecTable table;
    table.fields.push_back({.fieldNumber = 10, .typeId = 0});
    // oneof with arms101,102
    table.oneofs.push_back({.fieldStart = 0, .fieldCount = 2});
    table.oneofFieldNumbers.push_back(101);
    table.oneofFieldNumbers.push_back(102);

    ao::pack::byte::WriteStream ws{std::span<std::byte>(data.data(), data.size())};
    DiskEncodeCodec<ao::pack::byte::WriteStream> enc{table, ws};

    enc.msgBegin(0);
    enc.fieldBegin(0);
    enc.fieldId(0);
    enc.oneofEnter(0);
    // Manually encode an unknown arm id (e.g.,999)
    ao::pack::encodePrefixInt(ws, (uint64_t)999);
    enc.oneofExit();
    enc.fieldEnd();
    enc.msgEnd();

    REQUIRE(enc.ok());

    ao::pack::byte::ReadStream rs{std::span<std::byte const>(data.data(), ws.byteSize())};
    DiskDecodeCodec<ao::pack::byte::ReadStream> dec{table, rs};

    dec.msgBegin(0);
    dec.fieldBegin(0);
    REQUIRE(dec.fieldId(0));
    dec.oneofEnter(0);
    auto arm = dec.oneofArm(0, 8);
    REQUIRE(arm == std::numeric_limits<uint32_t>::max());
    // Exiting should still succeed
    dec.oneofExit();
    dec.fieldEnd();
    dec.msgEnd();

    REQUIRE(dec.ok());
}

TEST_CASE("Disk codec malformed array element tag yields BadData", "[disk][codec][malformed]") {
    std::vector<std::byte> data(256);
    ao::schema::codec::CodecTable table;
    table.fields.push_back({.fieldNumber = 7, .typeId = 0});

    ao::pack::byte::WriteStream ws{std::span<std::byte>(data.data(), data.size())};
    DiskEncodeCodec<ao::pack::byte::WriteStream> enc{table, ws};

    enc.msgBegin(0);
    enc.fieldBegin(0);
    enc.fieldId(0);
    enc.arrayBegin();
    // Write an invalid element-type tag as a prefix-int
    ao::pack::encodePrefixInt(ws, (uint64_t)255);
    enc.arrayLen(0, 1);
    // write one element (but decoder should fail on element tag)
    enc.u64(0, 5);
    enc.arrayEnd();
    enc.fieldEnd();
    enc.msgEnd();

    REQUIRE(enc.ok());

    ao::pack::byte::ReadStream rs{std::span<std::byte const>(data.data(), ws.byteSize())};
    DiskDecodeCodec<ao::pack::byte::ReadStream> dec{table, rs};

    dec.msgBegin(0);
    dec.fieldBegin(0);
    REQUIRE(dec.fieldId(0));
    // arrayBegin should fail because element tag is invalid
    dec.arrayBegin();
    REQUIRE_FALSE(dec.ok());
    REQUIRE(dec.error() == ao::pack::Error::BadData);
}
