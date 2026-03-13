#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "ao/pack/ByteStream.h"
#include "ao/pack/Error.h"
#include "ao/pack/Varint.h"
#include "ao/pack/ZigZag.h"
#include "ao/schema/CodecCommon.h"

namespace ao::schema::codec::disk {

enum class DiskTag : uint8_t {
    MsgBegin,
    Field,

    OneofBegin,
    ArrayBegin,  // Used for strings, bytes, arrays etc
    OptBegin,

    End,  // End for higher ordered types

    Fixed8,   // char, u8
    Fixed32,  // f32, f64 and related
    Fixed64,  // f32, f64 and related
    Varint,   // i64, u64
    DiskTagMax,
    Unknown = std::numeric_limits<uint8_t>::max(),
};

template <class OutStream>
class DiskEncodeCodec {
   public:
    DiskEncodeCodec(CodecTable const& table, OutStream& stream)
        : m_codec(table), m_stream(stream) {}

    using ChunkSize = CodecBytes;

    bool ok() const { return m_error == ao::pack::Error::Ok; }
    ao::pack::Error error() const { return m_error; }

    void msgBegin(uint32_t msgId) { writeTag(DiskTag::MsgBegin); }
    void msgEnd() { writeTag(DiskTag::End); }

    void fieldBegin(uint32_t fieldId) { writeTag(DiskTag::Field); }
    void fieldEnd() { writeTag(DiskTag::End); }

    void fieldId(uint32_t fieldId) {
        ao::pack::encodePrefixInt(m_stream,
                                  m_codec.fields[fieldId].fieldNumber);
    }

    void optBegin() { writeTag(DiskTag::OptBegin); }
    void optEnd() { writeTag(DiskTag::End); }
    void present(bool) {}

    void boolean(bool value) {
        writeTag(DiskTag::Varint);
        ao::pack::encodePrefixInt(m_stream, (uint64_t)value);
    }
    void u64(uint32_t /* width */, uint64_t value) {
        writeTag(DiskTag::Varint);
        ao::pack::encodePrefixInt(m_stream, (uint64_t)value);
    }
    void i64(uint32_t width, int64_t value) {
        writeTag(DiskTag::Varint);
        uint64_t v = ao::pack::encodeZigZag(value);
        ao::pack::encodePrefixInt(m_stream, v);
    }
    void f32(float v) {
        writeTag(DiskTag::Fixed32);
        static constexpr auto size = sizeof(float);
        static_assert(size == 4);
        m_stream.bytes(std::span{(std::byte*)&v, size}, size);
    }
    void f64(double v) {
        writeTag(DiskTag::Fixed64);
        static constexpr auto size = sizeof(double);
        static_assert(size == 8);
        m_stream.bytes(std::span{(std::byte*)&v, size}, size);
    }

    void arrayBegin(uint32_t /* typeId*/) { writeTag(DiskTag::ArrayBegin); }
    void arrayEnd() { writeTag(DiskTag::End); }
    void arrayLen(uint32_t width, uint32_t length) {
        ao::pack::encodePrefixInt(m_stream, length);
    }

    void oneofEnter(uint32_t oneofId) { writeTag(DiskTag::OneofBegin); }
    void oneofExit() { writeTag(DiskTag::End); }
    void oneofArm(uint32_t oneofId, uint64_t armId) {
        auto fieldNumberOffset = m_codec.oneofs[oneofId].fieldStart + armId;
        auto id = m_codec.oneofFieldNumbers[fieldNumberOffset];
        ao::pack::encodePrefixInt(m_stream, id);
    }

   private:
    void writeTag(DiskTag tag) {
        auto data = (std::byte)tag;
        m_stream.bytes(std::span<std::byte>{&data, 1}, 1);
    }

    ao::pack::Error fail(ao::pack::Error err) {
        if (!ok())
            return;
        m_error = err;
    }
    ao::pack::Error m_error = ao::pack::Error::Ok;

    CodecTable const& m_codec;
    OutStream& m_stream;
};
static_assert(CodecEncode<DiskEncodeCodec<ao::pack::byte::WriteStream>>);

template <class InStream>
class DiskDecodeCodec {
   public:
    DiskDecodeCodec(CodecTable const& table, InStream& stream)
        : m_codec(table), m_stream(stream) {}
    using ChunkSize = CodecBytes;
    bool ok() const { return error() == ao::pack::Error::Ok; }
    ao::pack::Error error() const { return m_error; }

    void msgBegin(uint32_t msgId) { readTag(DiskTag::MsgBegin); }
    void msgEnd() { readTag(DiskTag::End); }

    void fieldBegin(uint32_t fieldId) { readTag(DiskTag::Field); }
    void fieldEnd() { readTag(DiskTag::End); }

    bool fieldId(uint32_t fieldId) {
        uint64_t expected = m_codec.fields[fieldId].fieldNumber;
        uint64_t fieldNum = readVarint();
        if (!ok())
            return false;
        return expected == fieldNum;
    }

    // Skip from previous information
    // We are expecting an END eventually
    // Current processing a value
    bool skipField(uint32_t fieldId) {
        if (!skipFieldImpl())
            return false;
        // Expect the end tag after parsing the value
        return readTag(DiskTag::End);
    }

    bool boolean() { return readTaggedVarint(DiskTag::Varint) != 0; }
    uint64_t u64(uint16_t /*  width */) {
        return readTaggedVarint(DiskTag::Varint);
    }
    int64_t i64(uint16_t /*  width */) {
        auto v = readTaggedVarint(DiskTag::Varint);
        return ao::pack::decodeZigZag(v);
    }
    float f32() {
        if (!readTag(DiskTag::Fixed32))
            return 0.f;
        return fixed<float, 4>();
    }
    double f64() {
        if (!readTag(DiskTag::Fixed64))
            return 0.0;
        return fixed<double, 8>();
    }

    void arrayBegin(uint32_t /* typeId */) {
        readTag(DiskTag::ArrayBegin);

        // Read the type tag, as we aren't skipping it doesn't matter
        // readTag();
    }
    void arrayEnd() { readTag(DiskTag::End); }
    uint32_t arrayLen(uint32_t width) {
        uint64_t value = 0;
        ao::pack::decodePrefixInt(m_stream, value);
        raiseError();
        if (value > std::numeric_limits<uint32_t>::max()) {
            fail(ao::pack::Error::BadData);
            return 0;
        }

        return (uint32_t)value;
    }

    void optBegin() { readTag(DiskTag::OptBegin); }
    void optEnd() { readTag(DiskTag::End); }
    bool present() {
        std::byte byte;
        if (!m_stream.peek({&byte, 1}, 1)) {
            fail(ao::pack::Error::BadData);
            return false;
        }
        return static_cast<DiskTag>(byte) != DiskTag::End;
    }

    void oneofEnter(uint32_t oneofId) { readTag(DiskTag::OneofBegin); }
    void oneofExit() { readTag(DiskTag::End); }
    uint32_t oneofArm(uint32_t oneofId) {
        uint64_t value = 0;
        if (!ao::pack::decodePrefixInt(m_stream, value)) {
            raiseError();
            return 0;
        }
        if (value > std::numeric_limits<uint32_t>::max() || !ok()) {
            fail(ao::pack::Error::BadData);
            return 0;
        }

        auto start = m_codec.oneofs[oneofId].fieldStart;
        auto size = m_codec.oneofs[oneofId].fieldCount;
        for (uint32_t i = 0; i < size; ++i) {
            auto fieldNum = m_codec.oneofFieldNumbers[start + i];
            if (fieldNum == value)
                return i;
        }

        // Unknown size, return failure case
        return std::numeric_limits<uint32_t>::max();
    }

   private:
    template <class T, size_t Size>
    T fixed() {
        static constexpr auto size = sizeof(T);
        static_assert(sizeof(T) == Size);
        T ret = 0;
        m_stream.bytes(std::span{(std::byte*)&ret, size}, size);
        raiseError();
        return ret;
    }
    uint64_t readVarint() {
        uint64_t value = 0;
        if (!ao::pack::decodePrefixInt(m_stream, value)) {
            raiseError();
        }
        return value;
    }
    uint64_t readTaggedVarint(DiskTag expectedTag) {
        if (!readTag(expectedTag))
            return 0;
        return readVarint();
    }
    DiskTag readTag() {
        if (!ok())
            return DiskTag::Unknown;
        uint64_t out = 0;
        if (!ao::pack::decodePrefixInt(m_stream, out)) {
            fail(ao::pack::Error::BadData);
            return DiskTag::Unknown;
        }
        if (out >= (uint64_t)DiskTag::DiskTagMax) {
            fail(ao::pack::Error::BadData);
            return DiskTag::Unknown;
        }
        return static_cast<DiskTag>(out);
    }
    bool readTag(DiskTag expected) {
        auto tag = readTag();
        if (!ok())
            return false;
        if (tag != expected) {
            fail(ao::pack::Error::BadData);
            return false;
        }
        return true;
    }

    void raiseError() {
        if (m_stream.ok())
            return;
        if (!ok())
            return;
        m_error = m_stream.error();
    }

    bool skipMsg() {
        // This skips the _body_ of the message
        auto tag = readTag();
        while (ok() && tag == DiskTag::Field) {
            if (!skipFieldImpl())
                return false;
            tag = readTag();
        }
        return tag == DiskTag::End;
    }

    bool skipOneof() {
        // Skip who cares
        uint64_t fieldId = readVarint();

        // Encoded as a field from here
        return skipFieldImpl();
    }
    bool skipArray() {
        uint64_t len = readVarint();
        for (size_t i = 0; i < len; ++i) {
            // TODO lift the readtag to the start of the array
            // Format for array should be ARRAY TYPE LEN ... ITEMS ... END
            auto tag = readTag();
            if (!skipFieldImpl(tag))
                return false;
        }
        return readTag(DiskTag::End);
    }
    bool skipOpt() {
        auto tag = readTag();
        if (tag == DiskTag::End)
            return true;
        if (!skipFieldImpl(tag))
            return false;
        return readTag(DiskTag::End);
    }

    bool skipFieldImpl(DiskTag tag) {
        if (!ok())
            return false;

        switch (tag) {
            case DiskTag::MsgBegin:
                return skipMsg();

            case DiskTag::Field:
                // Invalid a value cannot start with a field!
                fail(ao::pack::Error::BadData);
                return false;

            case DiskTag::OneofBegin:
                return skipOneof();

            case DiskTag::ArrayBegin:
                // Used for strings, bytes, arrays etc
                return skipArray();

            case DiskTag::OptBegin:
                return skipOpt();

            case DiskTag::End:
                fail(ao::pack::Error::BadData);
                return false;

            case DiskTag::Fixed32:
                f32();
                return ok();
            case DiskTag::Fixed64:
                f64();
                return ok();
            case DiskTag::Varint:
                readVarint();
                return ok();
            default:
                return false;
        }

        return true;
    }
    bool skipFieldImpl() { return skipFieldImpl(readTag()); }

    ao::pack::Error fail(ao::pack::Error err) {
        if (!ok())
            return m_error;
        m_error = err;
        return m_error;
    }

    ao::pack::Error m_error = ao::pack::Error::Ok;
    CodecTable const& m_codec;
    InStream& m_stream;
};

static_assert(CodecDecode<DiskDecodeCodec<ao::pack::byte::ReadStream>>);
}  // namespace ao::schema::codec::disk
