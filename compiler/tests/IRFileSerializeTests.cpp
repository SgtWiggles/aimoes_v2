#include <catch2/catch_all.hpp>

#include "ao/pack/ByteStream.h"
#include "ao/schema/IR.h"
#include "ao/schema/Serializer.h"

using namespace ao::pack::byte;
using namespace ao::schema;
using namespace ao::schema::ir;

static std::vector<std::byte> writeIRToBuffer(IR const& ir) {
    // sizing pass
    SizeWriteStream sizer;
    serialize(sizer, IRHeader{});
    // serialize payload + hash using the public API to get size
    serializeIRFile(sizer, ir);
    REQUIRE(sizer.ok());
    auto total = sizer.byteSize();

    std::vector<std::byte> buf(total);
    WriteStream writer(std::span(buf.data(), buf.size()));
    bool ok = serializeIRFile(writer, ir);
    REQUIRE(ok);
    return buf;
}

TEST_CASE("serializeIRFile / deserializeIRFile roundtrip", "[irfile]") {
    IR ir;
    ir.strings = {"one", "two"};
    // populate small IR to ensure non-empty payload
    Field f;
    f.name = IdFor<std::string>{0};
    f.fieldNumber = 1;
    f.type = IdFor<Type>{0};
    f.directives = IdFor<DirectiveSet>{0};
    ir.fields.push_back(f);

    auto buf = writeIRToBuffer(ir);

    // Read back using byte readstream
    ReadStream reader(std::span(buf.data(), buf.size()));

    IR out;
    bool ok = deserializeIRFile(reader, out);
    REQUIRE(ok);
    REQUIRE(out.strings.size() == ir.strings.size());
    REQUIRE(out.fields.size() == ir.fields.size());
    REQUIRE(out.strings[0] == "one");
}

TEST_CASE("deserializeIRFile detects bad header", "[irfile]") {
    // Create buffer with corrupted header (wrong magic)
    SizeWriteStream sizer;
    // write incorrect header bytes directly
    std::array<std::byte, 16> badHeader{};
    // fill zeros (invalid magic)
    serialize(sizer, badHeader);
    auto total = sizer.byteSize();
    std::vector<std::byte> buf(total);
    WriteStream writer(std::span(buf.data(), buf.size()));
    serialize(writer, badHeader);

    ReadStream reader(std::span(buf.data(), buf.size()));

    IR out;
    bool ok = deserializeIRFile(reader, out);
    REQUIRE(ok == false);
}

TEST_CASE("deserializeIRFile detects bad hash", "[irfile]") {
    IR ir;
    ir.strings = {"abc"};
    std::vector<std::byte> buf;
    {
        // create proper file but then corrupt hash
        SizeWriteStream sizer;
        serialize(sizer, IRHeader{});
        serializeIRFile(sizer, ir);
        auto total = sizer.byteSize();
        buf.resize(total);
        WriteStream writer(std::span(buf.data(), buf.size()));
        serialize(writer, IRHeader{});
        ao::pack::HashingStream<WriteStream, ao::utils::hash::Blake3Hasher> hs(
            writer);
        hs.enableHashing();
        serialize(hs, ir);
        // write intentionally wrong hash (all zeros)
        std::array<std::byte, 32> wrong{};
        serialize(writer, wrong);
    }

    ReadStream reader(std::span(buf.data(), buf.size()));
    IR out;
    bool ok = deserializeIRFile(reader, out);
    REQUIRE(ok == false);
}
