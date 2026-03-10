#pragma once

#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "ao/pack/BitStream.h"
#include "ao/pack/ByteStream.h"
#include "ao/pack/Error.h"
#include "ao/pack/Varint.h"
#include "ao/pack/ZigZag.h"

#include "ao/schema/NetCodec.h"

namespace ao::schema::vm {

// Protobuf-like wire kinds
enum WireKind : uint8_t {
    WireVarint = 0,
    Wire64 = 1,
    WireLengthDelimited = 2,
    // 3 and 4 are groups (deprecated)
    Wire32 = 5,
};

// Header-only, template codecs.
// encode/decode a protobuf-like on-the-wire format using the
// `net.fields[fieldId].fieldNumber` as the protobuf field number.
//
// Notes / design choices (simple, conservative):
// - Scalar "variable width" fields (bit width == 0) are encoded as varints.
// - Signed integers use ZigZag encoding when written as varints.
// - 32-bit/64-bit fixed widths map to fixed32/fixed64 wire kinds.
// - Arrays are encoded as repeated fields (each element has its own tag).
//   Packed (length-delimited) repeated is supported on decode if encountered
//   (it will be unpacked into a temporary buffer).
// - The implementation attempts to be tolerant when decoding unknown fields by
//   providing `skipFieldId` which consumes the payload.

template <class OutStream>
struct DiskEncodeCodec {
    using ChunkSize = CodecBits;

    OutStream& out;
    CodecTable const& net;

    DiskEncodeCodec(OutStream& o, CodecTable const& n) : out(o), net(n) {}

    void msgBegin(uint32_t msgId) { (void)msgId; }
    void msgEnd() {}

    void fieldBegin(uint32_t fieldId) { currentFieldId = fieldId; }
    void fieldEnd() { currentFieldId = UINT32_MAX; }
    void fieldId(uint32_t fieldId) { fieldBegin(fieldId); }

    void present(bool) {}  // protobuf-like has no inline presence bit.
    void align() {}

    void boolean(bool v) {
        emitTagAndThen(WireVarint, [&](void) {
            ao::pack::encodePrefixInt(out, v ? 1u : 0u);
        });
    }

    // ALWAYS encode integers as varints (ignore declared `bw`).
    void u64(uint32_t /*bw*/, uint64_t v) {
        emitTagAndThen(WireVarint,
                       [&] { ao::pack::encodePrefixInt(out, v); });
    }

    void i64(uint32_t /*bw*/, int64_t v) {
        // Always encode signed integers using ZigZag + varint regardless of bw.
        uint64_t z = ao::pack::encodeZigZag(v);
        emitTagAndThen(WireVarint,
                       [&] { ao::pack::encodePrefixInt(out, z); });
    }

    void f32(float f) {
        emitTagAndThen(Wire32, [&] {
            uint32_t bits = std::bit_cast<uint32_t>(f);
            std::array<std::byte, 4> buf;
            std::memcpy(buf.data(), &bits, 4);
            out.bytes(std::span<std::byte>(buf.data(), buf.size()), buf.size());
        });
    }

    void f64(double d) {
        emitTagAndThen(Wire64, [&] {
            uint64_t bits = std::bit_cast<uint64_t>(d);
            std::array<std::byte, 8> buf;
            std::memcpy(buf.data(), &bits, 8);
            out.bytes(std::span<std::byte>(buf.data(), buf.size()), buf.size());
        });
    }

    // Arrays: we implement repeated elements (each element is encoded with
    // its tag). We don't pack by default here; callers will invoke element
    // writes which will write their own tag.
    void arrayBegin() { inArray = true; }
    void arrayEnd() { inArray = false; }
    void arrayLen(uint32_t /*width*/, uint32_t /*len*/) {
        // No-op for non-packed repeated encoding.
    }

    void oneofEnter(uint32_t) {}
    void oneofExit() {}
    void oneofArm(uint32_t /*width*/, uint64_t armid) {
        // Oneof arm is encoded as a varint value on the current field.
        emitTagAndThen(WireVarint,
                       [&] { ao::pack::encodePrefixInt(out, armid); });
    }

    bool ok() const { return out.ok(); }
    ao::pack::Error error() const { return out.error(); }

   private:
    uint32_t currentFieldId = UINT32_MAX;
    bool inArray = false;

    template <class F>
    void emitTagAndThen(uint8_t kind, F fn) {
        if (currentFieldId == UINT32_MAX) {
            // nothing to tag against; avoid crash but mark stream error
            out.require(false, ao::pack::Error::BadData);
            return;
        }
        uint64_t fieldNumber = net.fields[currentFieldId].fieldNumber;
        uint64_t tag = (fieldNumber << 3) | (tag_t)kind;
        ao::pack::encodePrefixInt(out, tag);
        fn();
    }

    using tag_t = uint64_t;
};

template <class InStream>
struct DiskDecodeCodec {
    using ChunkSize = CodecBytes;

    InStream& in;
    CodecTable const& net;

    DiskDecodeCodec(InStream& i, CodecTable const& n)
        : in(i), net(n), m_tagCached(false), m_lastWire(0), m_inPacked(false) {}

    void msgBegin(uint32_t msgId) { (void)msgId; }
    void msgEnd() {}

    void fieldBegin(uint32_t fieldId) { currentFieldId = fieldId; }
    void fieldEnd() { currentFieldId = UINT32_MAX; }

    // Attempt to match the next on-wire field to requested `fieldId`.
    // Returns true and primes the subsequent value reader if matched;
    // returns false if the next tag does not match.
    bool fieldId(uint32_t fieldId) {
        currentFieldId = fieldId;
        if (!readTag())
            return false;
        uint64_t expected = net.fields[fieldId].fieldNumber;
        if (m_cachedFieldNumber != expected) {
            // do not consume tag here; caller can call skipFieldId() to consume
            return false;
        }
        // Consume the tag and record wire kind for value readers.
        m_tagCached = false;
        m_lastWire = m_cachedWireKind;
        return true;
    }

    bool skipFieldId(uint32_t /*fieldId*/) {
        if (!m_tagCached) {
            if (!readTag())
                return false;
        }
        bool ok = skipWireKind(m_cachedWireKind);
        m_tagCached = false;
        return ok;
    }

    bool present() {
        // Not encoded in protobuf-like format
        return true;
    }

    void align() {}

    bool boolean() {
        uint64_t v = 0;
        if (!readVarForValue(v))
            return false;
        return (v & 1u) != 0;
    }

    uint64_t u64(uint32_t width) {
        uint64_t v = 0;
        if (!readValueForUInt(width, v))
            return 0;
        return v;
    }

    int64_t i64(uint32_t bw) {
        uint64_t u = 0;
        if (!readValueForUInt(bw, u))
            return 0;
        if (bw == 0) {
            return ao::pack::decodeZigZag(u);
        } else {
            return static_cast<int64_t>(u);
        }
    }

    float f32() {
        uint32_t bits = 0;
        if (!readFixed32(bits))
            return 0.0f;
        return std::bit_cast<float>(bits);
    }

    double f64() {
        uint64_t bits = 0;
        if (!readFixed64(bits))
            return 0.0;
        return std::bit_cast<double>(bits);
    }

    void arrayBegin() {}
    void arrayEnd() { endPackedIfActive(); }

    // If the on-wire representation is length-delimited packed primitives,
    // this will extract the packed payload and return the number of elements.
    // For non-packed repeated elements (each item with own tag) callers will
    // typically call `fieldId` repeatedly and arrayLen won't be used.
    uint32_t arrayLen(uint32_t width) {
        if (!readTag())
            return 0;
        if (m_cachedWireKind != WireLengthDelimited) {
            // Not a packed length-delimited payload; push the tag back and
            // indicate 0 (caller should use repeated fieldId approach).
            return 0;
        }
        // Consume the tag
        m_tagCached = false;

        uint64_t len = 0;
        if (!ao::pack::decodePrefixInt(in, len))
            return 0;
        // read the len bytes into buffer
        m_packedBuffer.resize(static_cast<size_t>(len));
        if (len != 0) {
            in.bytes(std::span<std::byte>(m_packedBuffer.data(),
                                          m_packedBuffer.size()),
                     static_cast<size_t>(len));
        }
        m_packedPos = 0;
        m_inPacked = true;

        if (width == 0) {
            // count varints in the buffer
            uint32_t count = 0;
            size_t pos = 0;
            while (pos < m_packedBuffer.size()) {
                // parse a single varint
                do {
                    if (pos >= m_packedBuffer.size())
                        break;
                    uint8_t b = static_cast<uint8_t>(m_packedBuffer[pos]);
                    ++pos;
                    if ((b & 0x80) == 0)
                        break;
                } while (true);
                ++count;
            }
            return count;
        } else {
            // fixed-width elements: number of elements = (len /
            // bytesPerElement)
            size_t bytesPer = (width + 7) / 8;
            if (bytesPer == 0)
                bytesPer = 1;
            return static_cast<uint32_t>(m_packedBuffer.size() / bytesPer);
        }
    }

    void oneofEnter(uint32_t) {}
    void oneofExit() {}
    uint32_t oneofArm(uint32_t /*oneofId*/, uint32_t /*width*/) {
        uint64_t v = 0;
        if (!readVarForValue(v))
            return 0;
        return static_cast<uint32_t>(v);
    }

    bool ok() const { return in.ok(); }
    ao::pack::Error error() const { return in.error(); }

    bool skip() { return false; }

   private:
    uint32_t currentFieldId = UINT32_MAX;

    // Tag caching
    bool m_tagCached;
    uint64_t m_cachedTag = 0;
    uint64_t m_cachedFieldNumber = 0;
    uint8_t m_cachedWireKind = 0;

    // last matched wire kind (after fieldId())
    uint8_t m_lastWire;

    // Packed (length-delimited) decoded buffer support
    std::vector<std::byte> m_packedBuffer;
    size_t m_packedPos = 0;
    bool m_inPacked = false;

    bool readTag() {
        if (m_tagCached)
            return true;
        uint64_t tag = 0;
        if (!ao::pack::decodePrefixInt(in, tag))
            return false;
        m_cachedTag = tag;
        m_cachedFieldNumber = (tag >> 3);
        m_cachedWireKind = static_cast<uint8_t>(tag & 0x7u);
        m_tagCached = true;
        return true;
    }

    bool skipWireKind(uint8_t kind) {
        switch (kind) {
            case WireVarint: {
                uint64_t tmp = 0;
                return ao::pack::decodePrefixInt(in, tmp);
            }
            case Wire64: {
                std::array<std::byte, 8> buf;
                in.bytes(std::span<std::byte>(buf.data(), buf.size()),
                         buf.size());
                return in.ok();
            }
            case WireLengthDelimited: {
                uint64_t len = 0;
                if (!ao::pack::decodePrefixInt(in, len))
                    return false;
                if (len != 0) {
                    std::vector<std::byte> buf(static_cast<size_t>(len));
                    in.bytes(std::span<std::byte>(buf.data(), buf.size()),
                             buf.size());
                }
                return in.ok();
            }
            case Wire32: {
                std::array<std::byte, 4> buf;
                in.bytes(std::span<std::byte>(buf.data(), buf.size()),
                         buf.size());
                return in.ok();
            }
            default:
                in.require(false, ao::pack::Error::BadData);
                return false;
        }
    }

    // Utilities to read a varint either from packed buffer or main stream.
    bool readVarFromPacked(uint64_t& out) {
        out = 0;
        uint64_t shift = 0;
        unsigned iter = 0;
        while (m_packedPos < m_packedBuffer.size()) {
            uint8_t b = static_cast<uint8_t>(m_packedBuffer[m_packedPos++]);
            out |= (uint64_t(b & 0x7F) << shift);
            shift += 7;
            ++iter;
            if ((b & 0x80) == 0)
                break;
            if (iter > 10) {
                in.require(false, ao::pack::Error::BadData);
                return false;
            }
        }
        if (m_packedPos >= m_packedBuffer.size())
            m_inPacked = false;
        return true;
    }

    bool readVarForValue(uint64_t& out) {
        // If we have a packed buffer active, decode from it.
        if (m_inPacked) {
            return readVarFromPacked(out);
        }
        // If a tag is still cached but not yet consumed (fieldId not called),
        // consume it now and set last wire for callers that don't use
        // fieldId().
        if (m_tagCached) {
            m_lastWire = m_cachedWireKind;
            m_tagCached = false;
        }
        // Expect varint wire kind for the next value, but be permissive:
        if (m_lastWire != WireVarint && m_lastWire != 0) {
            // If lastWire was not set, attempt to read a tag to set it
            if (!readTag())
                return false;
            m_tagCached = false;
            m_lastWire = m_cachedWireKind;
        }
        return ao::pack::decodePrefixInt(in, out);
    }

    bool readValueForUInt(uint32_t width, uint64_t& out) {
        if (m_inPacked) {
            // packed primitives stored in buffer; read accordingly
            if (width == 0) {
                return readVarFromPacked(out);
            } else {
                size_t bytes = (width + 7) / 8;
                if (bytes == 0)
                    bytes = 1;
                if (m_packedPos + bytes > m_packedBuffer.size())
                    return false;
                uint64_t v = 0;
                for (size_t i = 0; i < bytes; ++i) {
                    v |= (uint64_t(static_cast<uint8_t>(
                             m_packedBuffer[m_packedPos++])))
                         << (8 * i);
                }
                if (m_packedPos >= m_packedBuffer.size())
                    m_inPacked = false;
                out = v;
                return true;
            }
        }

        // If a tag is cached (readTag() was previously called and not
        // consumed), interpret according to its wire kind. We now accept
        // varints even for non-zero widths (backwards-compatible with encoder
        // change).
        if (m_tagCached) {
            uint8_t kind = m_cachedWireKind;
            m_tagCached = false;  // consume tag
            if (kind == WireVarint) {
                return ao::pack::decodePrefixInt(in, out);
            } else if (kind == Wire64) {
                std::array<std::byte, 8> buf;
                in.bytes(std::span<std::byte>(buf.data(), buf.size()),
                         buf.size());
                if (!in.ok())
                    return false;
                uint64_t v = 0;
                std::memcpy(&v, buf.data(), 8);
                out = v;
                return true;
            } else if (kind == Wire32) {
                std::array<std::byte, 4> buf;
                in.bytes(std::span<std::byte>(buf.data(), buf.size()),
                         buf.size());
                if (!in.ok())
                    return false;
                uint32_t v = 0;
                std::memcpy(&v, buf.data(), 4);
                out = v;
                return true;
            } else if (kind == WireLengthDelimited) {
                uint64_t len = 0;
                if (!ao::pack::decodePrefixInt(in, len))
                    return false;
                std::vector<std::byte> buf(static_cast<size_t>(len));
                if (len != 0) {
                    in.bytes(std::span<std::byte>(buf.data(), buf.size()),
                             buf.size());
                    if (!in.ok())
                        return false;
                }
                // Interpret length-delimited payload as little-endian integer
                uint64_t v = 0;
                size_t bytes = buf.size();
                for (size_t i = 0; i < bytes && i < 8; ++i) {
                    v |= (uint64_t(static_cast<uint8_t>(buf[i])) << (8 * i));
                }
                out = v;
                return true;
            } else {
                in.require(false, ao::pack::Error::BadData);
                return false;
            }
        }

        // If a previous call consumed the tag and recorded the wire kind in
        // m_lastWire, honor it — but also accept varint even when width != 0.
        if (m_lastWire != 0) {
            uint8_t kind = m_lastWire;
            m_lastWire = 0;  // consume the remembered wire kind
            if (kind == WireVarint) {
                return ao::pack::decodePrefixInt(in, out);
            } else if (kind == Wire64) {
                std::array<std::byte, 8> buf;
                in.bytes(std::span<std::byte>(buf.data(), buf.size()),
                         buf.size());
                if (!in.ok())
                    return false;
                uint64_t v = 0;
                std::memcpy(&v, buf.data(), 8);
                out = v;
                return true;
            } else if (kind == Wire32) {
                std::array<std::byte, 4> buf;
                in.bytes(std::span<std::byte>(buf.data(), buf.size()),
                         buf.size());
                if (!in.ok())
                    return false;
                uint32_t v = 0;
                std::memcpy(&v, buf.data(), 4);
                out = v;
                return true;
            } else if (kind == WireLengthDelimited) {
                uint64_t len = 0;
                if (!ao::pack::decodePrefixInt(in, len))
                    return false;
                std::vector<std::byte> buf(static_cast<size_t>(len));
                if (len != 0) {
                    in.bytes(std::span<std::byte>(buf.data(), buf.size()),
                             buf.size());
                    if (!in.ok())
                        return false;
                }
                uint64_t v = 0;
                size_t bytes = buf.size();
                for (size_t i = 0; i < bytes && i < 8; ++i) {
                    v |= (uint64_t(static_cast<uint8_t>(buf[i])) << (8 * i));
                }
                out = v;
                return true;
            } else {
                in.require(false, ao::pack::Error::BadData);
                return false;
            }
        }

        // No cached tag and no remembered wire kind — attempt to read a tag to
        // determine the wire kind, then handle it. This allows decoding varints
        // even when the declared width is non-zero.
        if (!readTag())
            return false;
        uint8_t kind = m_cachedWireKind;
        m_tagCached = false;  // consume tag
        if (kind == WireVarint) {
            return ao::pack::decodePrefixInt(in, out);
        } else if (kind == Wire64) {
            std::array<std::byte, 8> buf;
            in.bytes(std::span<std::byte>(buf.data(), buf.size()), buf.size());
            if (!in.ok())
                return false;
            uint64_t v = 0;
            std::memcpy(&v, buf.data(), 8);
            out = v;
            return true;
        } else if (kind == Wire32) {
            std::array<std::byte, 4> buf;
            in.bytes(std::span<std::byte>(buf.data(), buf.size()), buf.size());
            if (!in.ok())
                return false;
            uint32_t v = 0;
            std::memcpy(&v, buf.data(), 4);
            out = v;
            return true;
        } else if (kind == WireLengthDelimited) {
            uint64_t len = 0;
            if (!ao::pack::decodePrefixInt(in, len))
                return false;
            std::vector<std::byte> buf(static_cast<size_t>(len));
            if (len != 0) {
                in.bytes(std::span<std::byte>(buf.data(), buf.size()),
                         buf.size());
                if (!in.ok())
                    return false;
            }
            uint64_t v = 0;
            size_t bytes = buf.size();
            for (size_t i = 0; i < bytes && i < 8; ++i) {
                v |= (uint64_t(static_cast<uint8_t>(buf[i])) << (8 * i));
            }
            out = v;
            return true;
        } else {
            in.require(false, ao::pack::Error::BadData);
            return false;
        }
    }

    bool readFixed32(uint32_t& out) {
        if (m_inPacked) {
            size_t need = 4;
            if (m_packedPos + need > m_packedBuffer.size())
                return false;
            uint32_t v = 0;
            std::memcpy(&v, m_packedBuffer.data() + m_packedPos, need);
            m_packedPos += need;
            if (m_packedPos >= m_packedBuffer.size())
                m_inPacked = false;
            out = v;
            return true;
        }
        if (m_tagCached) {
            m_lastWire = m_cachedWireKind;
            m_tagCached = false;
        }
        std::array<std::byte, 4> buf;
        in.bytes(std::span<std::byte>(buf.data(), buf.size()), buf.size());
        if (!in.ok())
            return false;
        std::memcpy(&out, buf.data(), 4);
        return true;
    }

    bool readFixed64(uint64_t& out) {
        if (m_inPacked) {
            size_t need = 8;
            if (m_packedPos + need > m_packedBuffer.size())
                return false;
            uint64_t v = 0;
            std::memcpy(&v, m_packedBuffer.data() + m_packedPos, need);
            m_packedPos += need;
            if (m_packedPos >= m_packedBuffer.size())
                m_inPacked = false;
            out = v;
            return true;
        }
        if (m_tagCached) {
            m_lastWire = m_cachedWireKind;
            m_tagCached = false;
        }
        std::array<std::byte, 8> buf;
        in.bytes(std::span<std::byte>(buf.data(), buf.size()), buf.size());
        if (!in.ok())
            return false;
        std::memcpy(&out, buf.data(), 8);
        return true;
    }

    void endPackedIfActive() {
        m_inPacked = false;
        m_packedBuffer.clear();
        m_packedPos = 0;
    }
};

}  // namespace ao::schema::vm