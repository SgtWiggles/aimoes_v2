#pragma once

#include <catch2/catch_all.hpp>

#include <span>

#include <ao/pack/BitStream.h>
#include <ao/pack/ByteStream.h>

#include <ao/schema/CppAdapter.h>
#include <ao/schema/VM.h>

#include <ao/schema/VMPrettyPrint.h>

template <class T>
ao::schema::vm::VM encodeCpp(ao::schema::vm::Format const& format,
                             ao::schema::codec::CodecTable const& codecTable,
                             ao::pack::bit::WriteStream& stream,
                             T const& input) {
    ao::schema::cpp::CppEncodeAdapter object;
    object.setRoot(input);
    ao::schema::codec::net::NetEncodeCodec codec{codecTable, stream};

    auto machine = ao::schema::vm::VM{&format.encode};
    ao::schema::vm::encode(machine, object, codec, T::AOSL_TYPE_ID);
    return machine;
}

template <class T>
ao::schema::vm::VM decodeCpp(ao::schema::vm::Format const& format,
                             ao::schema::codec::CodecTable const& codecTable,
                             ao::pack::bit::ReadStream& stream,
                             T& output) {
    ao::schema::cpp::CppDecodeAdapter object;
    object.setRoot(output);
    ao::schema::codec::net::NetDecodeCodec codec{codecTable, stream};

    auto machine = ao::schema::vm::VM{&format.decode};
    ao::schema::vm::decode(machine, object, codec, T::AOSL_TYPE_ID);
    return machine;
}

template <class T>
ao::schema::vm::VM encodeCpp(ao::schema::vm::Format const& format,
                             ao::schema::codec::CodecTable const& codecTable,
                             ao::pack::byte::WriteStream& stream,
                             T const& input) {
    ao::schema::cpp::CppEncodeAdapter object;
    object.setRoot(input);
    ao::schema::codec::disk::DiskEncodeCodec codec{codecTable, stream};

    auto machine = ao::schema::vm::VM{&format.encode};
    ao::schema::vm::encode(machine, object, codec, T::AOSL_TYPE_ID);
    return machine;
}

template <class T>
ao::schema::vm::VM decodeCpp(ao::schema::vm::Format const& format,
                             ao::schema::codec::CodecTable const& codecTable,
                             ao::pack::byte::ReadStream& stream,
                             T& output) {
    ao::schema::cpp::CppDecodeAdapter object;
    object.setRoot(output);
    ao::schema::codec::disk::DiskDecodeCodec codec{codecTable, stream};

    auto machine = ao::schema::vm::VM{&format.decode};
    ao::schema::vm::decode(machine, object, codec, T::AOSL_TYPE_ID);
    return machine;
}

template <class WS, class RS, class T>
void cppRoundTrip(std::span<std::byte const> ir, T const& input, T& output) {
    ao::pack::byte::ReadStream irRs{ir};
    ao::schema::ir::IR irBytecode;
    REQUIRE(ao::schema::ir::deserializeIRFile(irRs, irBytecode));
    auto codecTable = ao::schema::codec::generateCodecTable(irBytecode);

    ao::schema::ErrorContext errs;
    std::vector<std::byte> data(4096);

    auto format = ao::schema::vm::generateProgram(irBytecode, errs);
    REQUIRE(errs.ok());

    WS ws{std::span{data.data(), data.size()}};
    auto encoded = encodeCpp(format, codecTable, ws, input);
    {
        INFO("PC " << encoded.pc);
        INFO("ENCODE " << prettyPrint(format.encode));
        REQUIRE(encoded.error == ao::schema::vm::VMError::Ok);
        REQUIRE(ws.ok());
    }

    RS rs{{data.data(), ws.byteSize()}};
    auto decoded = decodeCpp(format, codecTable, rs, output);
    {
        INFO("PC " << decoded.pc);
        INFO("DECODE" << prettyPrint(format.decode));
        REQUIRE(decoded.error == ao::schema::vm::VMError::Ok);
        REQUIRE(rs.ok());
        REQUIRE(rs.remainingBytes() == 0);
    }
}

struct NetStreams {
    using WS = ao::pack::bit::WriteStream;
    using RS = ao::pack::bit::ReadStream;
};

struct DiskStreams {
    using WS = ao::pack::byte::WriteStream;
    using RS = ao::pack::byte::ReadStream;
};

using StreamTypes = std::tuple<NetStreams, DiskStreams>;
