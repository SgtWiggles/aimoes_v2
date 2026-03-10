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

namespace ao::schema::vm {

namespace tlv_detail {

inline void appendRaw(std::vector<std::byte>& dst, void const* src, size_t n) {
    auto const* first = static_cast<std::byte const*>(src);
    dst.insert(dst.end(), first, first + n);
}

struct VectorWriteStream {
    std::vector<std::byte>& buffer;
    ao::pack::Error status = ao::pack::Error::Ok;

    VectorWriteStream& bytes(std::span<std::byte> data, size_t count) {
        if (!ok())
            return *this;
        if (count > data.size()) {
            status = ao::pack::Error::BadArg;
            return *this;
        }
        buffer.insert(buffer.end(), data.begin(), data.begin() + count);
        return *this;
    }

    VectorWriteStream& require(bool cond, ao::pack::Error err) {
        if (!ok())
            return *this;
        if (!cond)
            status = err;
        return *this;
    }

    bool ok() const { return status == ao::pack::Error::Ok; }
    ao::pack::Error error() const { return status; }
};

inline bool encodeVarintToVector(std::vector<std::byte>& dst, uint64_t value) {
    VectorWriteStream ws{dst};
    ao::pack::encodePrefixInt(ws, value);
    return ws.ok();
}

template <class OutStream>
inline void writeVector(OutStream& out, std::vector<std::byte> const& data) {
    if (data.empty())
        return;
    std::span<std::byte> s(const_cast<std::byte*>(data.data()), data.size());
    out.bytes(s, s.size());
}

inline bool readExact(ao::pack::byte::ReadStream& in,
                      std::vector<std::byte>& dst,
                      size_t size) {
    dst.resize(size);
    if (size == 0)
        return true;
    std::span<std::byte> out(dst.data(), dst.size());
    in.bytes(out, size);
    return in.ok();
}

struct DecodeScope {
    ao::pack::byte::ReadStream stream{std::span<std::byte const>()};

    bool headerCached = false;
    uint64_t cachedFieldNumber = 0;
    uint64_t cachedByteCount = 0;

    std::vector<std::byte> currentValue;
    size_t currentValuePos = 0;

    bool arrayActive = false;
    std::vector<std::byte> arrayData;
    size_t arrayPos = 0;
    uint32_t arrayRemaining = 0;

    explicit DecodeScope(ao::pack::byte::ReadStream s) : stream(s) {}

    bool cacheHeader() {
        if (headerCached)
            return true;
        if (!ao::pack::decodePrefixInt(stream, cachedFieldNumber))
            return false;
        if (!ao::pack::decodePrefixInt(stream, cachedByteCount))
            return false;
        headerCached = true;
        return true;
    }

    void clearFieldState() {
        headerCached = false;
        cachedFieldNumber = 0;
        cachedByteCount = 0;
        currentValue.clear();
        currentValuePos = 0;
        arrayActive = false;
        arrayData.clear();
        arrayPos = 0;
        arrayRemaining = 0;
    }

    bool consumeCurrentField() {
        if (!headerCached && !cacheHeader())
            return false;
        headerCached = false;
        currentValuePos = 0;
        arrayActive = false;
        arrayData.clear();
        arrayPos = 0;
        arrayRemaining = 0;
        return readExact(stream, currentValue,
                         static_cast<size_t>(cachedByteCount));
    }

    bool skipCurrentField() {
        if (!headerCached && !cacheHeader())
            return false;
        std::vector<std::byte> scratch;
        bool ok =
            readExact(stream, scratch, static_cast<size_t>(cachedByteCount));
        headerCached = false;
        return ok;
    }

    ao::pack::byte::ReadStream currentValueStream() const {
        std::span<std::byte const> s(currentValue.data() + currentValuePos,
                                     currentValue.size() - currentValuePos);
        return ao::pack::byte::ReadStream(s);
    }

    ao::pack::byte::ReadStream arrayValueStream() const {
        std::span<std::byte const> s(arrayData.data() + arrayPos,
                                     arrayData.size() - arrayPos);
        return ao::pack::byte::ReadStream(s);
    }

    bool readCurrentBytes(void* dst, size_t n) {
        if (currentValuePos + n > currentValue.size())
            return false;
        std::memcpy(dst, currentValue.data() + currentValuePos, n);
        currentValuePos += n;
        return true;
    }

    bool readArrayBytes(void* dst, size_t n) {
        if (arrayPos + n > arrayData.size())
            return false;
        std::memcpy(dst, arrayData.data() + arrayPos, n);
        arrayPos += n;
        return true;
    }
};

}  // namespace tlv_detail

template <class OutStream>
struct DiskEncodeCodec {
    using ChunkSize = CodecBytes;

    struct EncodeScope {
        std::vector<std::byte> messageBytes;

        uint32_t currentFieldId = UINT32_MAX;

        bool inArray = false;
        uint32_t arrayCount = 0;
        std::vector<std::byte> arrayPayload;
    };

    OutStream& out;
    CodecTable const& net;

    std::vector<EncodeScope> scopes;

    DiskEncodeCodec(OutStream& o, CodecTable const& n) : out(o), net(n) {}

    void msgBegin(uint32_t /*msgId*/) { scopes.emplace_back(); }

    void msgEnd() {
        if (scopes.empty()) {
            out.require(false, ao::pack::Error::BadArg);
            return;
        }

        auto completed = std::move(scopes.back().messageBytes);
        scopes.pop_back();

        if (scopes.empty()) {
            tlv_detail::writeVector(out, completed);
            return;
        }

        auto& parent = scopes.back();
        emitFieldPayload(parent, completed);
    }

    void fieldBegin(uint32_t fieldId) {
        if (scopes.empty()) {
            out.require(false, ao::pack::Error::BadArg);
            return;
        }
        auto& s = scopes.back();
        s.currentFieldId = fieldId;
        s.inArray = false;
        s.arrayCount = 0;
        s.arrayPayload.clear();
    }

    void fieldEnd() {
        if (scopes.empty())
            return;
        auto& s = scopes.back();
        s.currentFieldId = UINT32_MAX;
        s.inArray = false;
        s.arrayCount = 0;
        s.arrayPayload.clear();
    }

    void fieldId(uint32_t fieldId) { fieldBegin(fieldId); }

    void present(bool /*present*/) {}
    void align() {}

    void boolean(bool v) {
        auto& s = currentScope();
        if (s.inArray) {
            s.arrayPayload.push_back(v ? std::byte{1} : std::byte{0});
            return;
        }
        std::vector<std::byte> payload{v ? std::byte{1} : std::byte{0}};
        emitFieldPayload(s, payload);
    }

    void u64(uint32_t /*width*/, uint64_t v) {
        auto& s = currentScope();
        std::vector<std::byte> payload;
        if (!tlv_detail::encodeVarintToVector(payload, v)) {
            out.require(false, ao::pack::Error::BadData);
            return;
        }

        if (s.inArray) {
            s.arrayPayload.insert(s.arrayPayload.end(), payload.begin(),
                                  payload.end());
            return;
        }

        emitFieldPayload(s, payload);
    }

    void i64(uint32_t /*width*/, int64_t v) {
        uint64_t z = ao::pack::encodeZigZag(v);
        u64(0, z);
    }

    void f32(float f) {
        auto& s = currentScope();
        uint32_t bits = std::bit_cast<uint32_t>(f);

        if (s.inArray) {
            tlv_detail::appendRaw(s.arrayPayload, &bits, sizeof(bits));
            return;
        }

        std::vector<std::byte> payload;
        tlv_detail::appendRaw(payload, &bits, sizeof(bits));
        emitFieldPayload(s, payload);
    }

    void f64(double d) {
        auto& s = currentScope();
        uint64_t bits = std::bit_cast<uint64_t>(d);

        if (s.inArray) {
            tlv_detail::appendRaw(s.arrayPayload, &bits, sizeof(bits));
            return;
        }

        std::vector<std::byte> payload;
        tlv_detail::appendRaw(payload, &bits, sizeof(bits));
        emitFieldPayload(s, payload);
    }

    void arrayBegin() {
        auto& s = currentScope();
        s.inArray = true;
        s.arrayCount = 0;
        s.arrayPayload.clear();
    }

    void arrayEnd() {
        auto& s = currentScope();
        if (!s.inArray)
            return;

        std::vector<std::byte> payload;
        if (!tlv_detail::encodeVarintToVector(payload, s.arrayCount)) {
            out.require(false, ao::pack::Error::BadData);
            return;
        }
        payload.insert(payload.end(), s.arrayPayload.begin(),
                       s.arrayPayload.end());

        s.inArray = false;
        s.arrayCount = 0;
        s.arrayPayload.clear();

        emitFieldPayload(s, payload);
    }

    void arrayLen(uint32_t /*width*/, uint32_t len) {
        currentScope().arrayCount = len;
    }

    void oneofEnter(uint32_t /*typeId*/) {}
    void oneofExit() {}

    void oneofArm(uint32_t /*width*/, uint64_t armId) { u64(0, armId); }

    bool ok() const { return out.ok(); }
    ao::pack::Error error() const { return out.error(); }

   private:
    EncodeScope& currentScope() {
        if (scopes.empty()) {
            out.require(false, ao::pack::Error::BadArg);
            static EncodeScope dummy;
            return dummy;
        }
        return scopes.back();
    }

    void emitFieldPayload(EncodeScope& scope,
                          std::vector<std::byte> const& payload) {
        if (scope.currentFieldId == UINT32_MAX) {
            out.require(false, ao::pack::Error::BadArg);
            return;
        }
        if (scope.currentFieldId >= net.fields.size()) {
            out.require(false, ao::pack::Error::BadArg);
            return;
        }

        uint64_t fieldNumber = net.fields[scope.currentFieldId].fieldNumber;
        if (!tlv_detail::encodeVarintToVector(scope.messageBytes,
                                              fieldNumber) ||
            !tlv_detail::encodeVarintToVector(scope.messageBytes,
                                              payload.size())) {
            out.require(false, ao::pack::Error::BadData);
            return;
        }

        scope.messageBytes.insert(scope.messageBytes.end(), payload.begin(),
                                  payload.end());
    }
};

template <class InStream>
struct DiskDecodeCodec {
    using ChunkSize = CodecBytes;

    InStream& in;
    CodecTable const& net;

    std::vector<std::byte> rootBytes;
    std::vector<tlv_detail::DecodeScope> scopes;

    DiskDecodeCodec(InStream& i, CodecTable const& n) : in(i), net(n) {}

    void msgBegin(uint32_t /*msgId*/) {
        if (scopes.empty()) {
            // Root message: snapshot remaining input into a byte-scope.
            size_t remaining = static_cast<size_t>(in.remainingBytes());
            if (!tlv_detail::readExact(in, rootBytes, remaining)) {
                in.require(false, in.error());
                return;
            }
            std::span<std::byte const> s(rootBytes.data(), rootBytes.size());
            scopes.emplace_back(ao::pack::byte::ReadStream(s));
            return;
        }

        auto& parent = currentScope();
        std::span<std::byte const> s(parent.currentValue.data(),
                                     parent.currentValue.size());
        scopes.emplace_back(ao::pack::byte::ReadStream(s));
    }

    void msgEnd() {
        if (scopes.empty()) {
            in.require(false, ao::pack::Error::BadArg);
            return;
        }
        scopes.pop_back();
    }

    void fieldBegin(uint32_t /*fieldId*/) {
        if (scopes.empty()) {
            in.require(false, ao::pack::Error::BadArg);
            return;
        }
        currentScope().clearFieldState();
    }

    void fieldEnd() {
        if (scopes.empty())
            return;
        currentScope().clearFieldState();
    }

    bool fieldId(uint32_t fieldId) {
        auto& s = currentScope();

        if (fieldId >= net.fields.size()) {
            in.require(false, ao::pack::Error::BadArg);
            return false;
        }
        if (!s.cacheHeader()) {
            in.require(false, s.stream.error());
            return false;
        }

        uint64_t expected = net.fields[fieldId].fieldNumber;
        if (s.cachedFieldNumber != expected)
            return false;

        if (!s.consumeCurrentField()) {
            in.require(false, s.stream.error());
            return false;
        }
        return true;
    }

    bool skipFieldId(uint32_t /*fieldId*/) {
        auto& s = currentScope();
        if (!s.skipCurrentField()) {
            in.require(false, s.stream.error());
            return false;
        }
        return true;
    }

    bool present() { return true; }
    void align() {}

    bool boolean() {
        auto& s = currentScope();

        if (s.arrayActive) {
            if (s.arrayRemaining == 0) {
                in.require(false, ao::pack::Error::Eof);
                return false;
            }

            std::byte b{};
            if (!s.readArrayBytes(&b, 1)) {
                in.require(false, ao::pack::Error::Eof);
                return false;
            }
            --s.arrayRemaining;
            return b != std::byte{0};
        }

        std::byte b{};
        if (!s.readCurrentBytes(&b, 1)) {
            in.require(false, ao::pack::Error::Eof);
            return false;
        }
        return b != std::byte{0};
    }

    uint64_t u64(uint32_t /*width*/) {
        auto& s = currentScope();

        if (s.arrayActive) {
            if (s.arrayRemaining == 0) {
                in.require(false, ao::pack::Error::Eof);
                return 0;
            }

            auto rs = s.arrayValueStream();
            uint64_t value = 0;
            if (!ao::pack::decodePrefixInt(rs, value)) {
                in.require(false, rs.error());
                return 0;
            }
            s.arrayPos = s.arrayData.size() - rs.remainingBytes();
            --s.arrayRemaining;
            return value;
        }

        auto rs = s.currentValueStream();
        uint64_t value = 0;
        if (!ao::pack::decodePrefixInt(rs, value)) {
            in.require(false, rs.error());
            return 0;
        }
        s.currentValuePos = s.currentValue.size() - rs.remainingBytes();
        return value;
    }

    int64_t i64(uint32_t /*width*/) { return ao::pack::decodeZigZag(u64(0)); }

    float f32() {
        auto& s = currentScope();
        uint32_t bits = 0;

        if (s.arrayActive) {
            if (s.arrayRemaining == 0 ||
                !s.readArrayBytes(&bits, sizeof(bits))) {
                in.require(false, ao::pack::Error::Eof);
                return 0.0f;
            }
            --s.arrayRemaining;
            return std::bit_cast<float>(bits);
        }

        if (!s.readCurrentBytes(&bits, sizeof(bits))) {
            in.require(false, ao::pack::Error::Eof);
            return 0.0f;
        }
        return std::bit_cast<float>(bits);
    }

    double f64() {
        auto& s = currentScope();
        uint64_t bits = 0;

        if (s.arrayActive) {
            if (s.arrayRemaining == 0 ||
                !s.readArrayBytes(&bits, sizeof(bits))) {
                in.require(false, ao::pack::Error::Eof);
                return 0.0;
            }
            --s.arrayRemaining;
            return std::bit_cast<double>(bits);
        }

        if (!s.readCurrentBytes(&bits, sizeof(bits))) {
            in.require(false, ao::pack::Error::Eof);
            return 0.0;
        }
        return std::bit_cast<double>(bits);
    }

    void arrayBegin() {}

    void arrayEnd() {
        auto& s = currentScope();
        s.arrayActive = false;
        s.arrayData.clear();
        s.arrayPos = 0;
        s.arrayRemaining = 0;
    }

    uint32_t arrayLen(uint32_t /*width*/) {
        auto& s = currentScope();

        auto rs = s.currentValueStream();
        uint64_t count = 0;
        if (!ao::pack::decodePrefixInt(rs, count)) {
            in.require(false, rs.error());
            return 0;
        }

        s.currentValuePos = s.currentValue.size() - rs.remainingBytes();
        s.arrayData.assign(s.currentValue.begin() +
                               static_cast<std::ptrdiff_t>(s.currentValuePos),
                           s.currentValue.end());
        s.arrayPos = 0;
        s.arrayRemaining = static_cast<uint32_t>(count);
        s.arrayActive = true;
        return s.arrayRemaining;
    }

    void oneofEnter(uint32_t /*typeId*/) {}
    void oneofExit() {}

    uint32_t oneofArm(uint32_t /*oneofId*/, uint32_t /*width*/) {
        return static_cast<uint32_t>(u64(0));
    }

    bool ok() const { return in.ok(); }
    ao::pack::Error error() const { return in.error(); }

   private:
    tlv_detail::DecodeScope& currentScope() {
        if (scopes.empty()) {
            in.require(false, ao::pack::Error::BadArg);
            static tlv_detail::DecodeScope dummy{
                ao::pack::byte::ReadStream(std::span<std::byte const>())};
            return dummy;
        }
        return scopes.back();
    }
};

static_assert(CodecEncode<DiskEncodeCodec<ao::pack::byte::WriteStream>>);
static_assert(CodecDecode<DiskDecodeCodec<ao::pack::byte::ReadStream>>);
}  // namespace ao::schema::vm