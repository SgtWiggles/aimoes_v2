#pragma once

#include <bit>
#include <cstdint>
#include <vector>

#include "ao/pack/BitStream.h"
#include "ao/pack/ByteStream.h"
#include "ao/pack/Varint.h"
#include "ao/pack/ZigZag.h"

#include "ao/schema/IR.h"

namespace ao::schema::vm {
// TODO generate to this, basically just a map of the bit widths of the
// individual fields OR variable width
struct CodecField {
    uint32_t fieldNumber;
    uint32_t typeId;
};
struct CodecType {
    uint8_t bitWidth;
    uint8_t flags;
};
struct CodecMessage {
    uint32_t fieldStart;
    uint32_t fieldCount;
};
struct CodecOneof {
    uint32_t fieldStart;
    uint32_t fieldCount;
};

struct CodecTable {
    std::vector<CodecType> types;
    std::vector<CodecMessage> messages;
    std::vector<CodecOneof> oneofs;

    std::vector<CodecField> fields;
};

struct CodecBytes {};
struct CodecBits {};

template <class OutStream>
struct NetEncodeCodec {
    using ChunkSize = CodecBits;

    OutStream& out;
    CodecTable const& net;

    // Message boundaries (presence bitmaps/alignment are codec concerns).
    void msgBegin(uint32_t msgId) {
        (void)msgId; /* e.g. align, reset presence accumulator */
    }
    void msgEnd() {
        // (void)msgId; /* flush presence bitmap if deferred */
    }

    void fieldBegin(uint32_t fieldId) { (void)fieldId; }
    void fieldEnd() {}
    void fieldId(uint32_t fieldId) { (void)fieldId; }

    void present(bool present) { out.bits(present ? 1u : 0u, 1); }
    void align() { out.align(); }

    void boolean(bool v) { out.bits(v ? 1u : 0u, 1); }
    void u64(uint32_t bw, uint64_t v) { out.bits(v, bw); }
    void i64(uint32_t bw, int64_t v) {
        // Common choice: two's complement in bw bits.
        out.bits(static_cast<uint64_t>(v), bw);
    }
    void f32(float f) {
        uint32_t bits = std::bit_cast<uint32_t>(f);
        out.bits(bits, 32);
    }
    void f64(double d) {
        uint64_t bits = std::bit_cast<uint64_t>(d);
        out.bits(bits, 64);
    }

    // Arrays: if net needs length prefix, write it here; otherwise no-op.
    void arrayBegin() {}
    void arrayEnd() {}
    void arrayLen(uint32_t width, uint32_t len) {
        if (width == 0) {
            ao::pack::encodeVarint(out, len);
        } else {
            out.bits(len, width);
        }
    }

    // Oneof: if net needs arm id encoded, do it here.
    // TODO change this to use oneofid and armid
    // The oneofid should carry all of the arm information required for this to
    // do it's work
    void oneofEnter(uint32_t typeId) {}
    void oneofExit() {}

    // Net format uses the compressed armid
    void oneofArm(uint32_t width, uint64_t armid) { out.bits(armid, width); }

    bool ok() const { return out.ok(); }
    ao::pack::Error error() const { return out.error(); }
};

template <class InStream>
struct NetDecodeCodec {
    using ChunkSize = CodecBits;

    InStream& in;
    CodecTable const& net;

    void msgBegin(uint32_t msgId) {
        (void)msgId; /* align, read presence bitmap if applicable */
    }
    void msgEnd() {}

    void fieldBegin(uint32_t fieldId) {}
    void fieldEnd() {}
    bool fieldId(uint32_t fieldId) { return true; }
    bool skipFieldId(uint32_t fieldId) { return false; }

    // If presence is inline:
    bool present() {
        uint64_t b = 0;
        in.bits(b, 1);
        return (b & 1u) != 0;
    }

    void align() { in.align(); }

    bool boolean() {
        uint64_t b = 0;
        in.bits(b, 1);
        return (b & 1u) != 0;
    }

    uint64_t u64(uint32_t width) {
        uint64_t v = 0;
        in.bits(v, width);
        return v;
    }

    int64_t i64(uint32_t bw) {
        uint64_t u = 0;
        in.bits(u, bw);
        // Sign-extend from bw bits.
        if (bw > 0 && bw < 64) {
            uint64_t sign = 1ull << (bw - 1);
            if (u & sign) {
                uint64_t mask = ~((1ull << bw) - 1);
                u |= mask;
            }
        }
        return static_cast<int64_t>(u);
    }

    float f32() {
        uint64_t u = 0;
        in.bits(u, 32);
        return std::bit_cast<float>(static_cast<uint32_t>(u));
    }

    double f64() {
        uint64_t bits = 0;
        in.bits(bits, 64);
        return std::bit_cast<double>(bits);
    }

    void arrayBegin() {}
    void arrayEnd() {}
    uint32_t arrayLen(uint32_t width) {
        uint64_t u = 0;
        if (width != 0) {
            in.bits(u, width);
        } else {
            ao::pack::decodeVarint(in, u);
        }
        return static_cast<uint32_t>(u);
    }

    void oneofEnter(uint32_t typeId) {}
    void oneofExit() {}
    uint32_t oneofArm(uint32_t oneofId, uint32_t width) {
        uint64_t u = 0;
        in.bits(u, width);
        return static_cast<uint32_t>(u);
    }

    bool ok() const { return in.ok(); }
    ao::pack::Error error() const { return in.error(); }

    bool skip() { return false; }
};
CodecTable generateCodecTable(ir::IR const& ir);

// TODO generate to this, basically just a list of fieldNumbers
// struct DiskField {
//     uint64_t fieldNumber;
// };
// struct DiskTables {
//     std::vector<DiskField> fields;
// };
// template <class OutStream>
// struct DiskEncodeCodec {
//     OutStream& out;
//     DiskTables const& disk;
//
//     void msg_begin(uint32_t /*msgId*/) {}
//     void msg_end(uint32_t /*msgId*/) {
//         // TODO flush out the size to another stream
//     }
//
//     void field_begin(uint32_t /*fieldId*/) {}
//     void field_end(uint32_t /*fieldId*/) {
//         // TODO flush out the size to another stream
//     }
//
//     void write_tag(uint32_t fieldId) {
//         const auto& fd = disk.fields[fieldId];
//         // Typical protobuf-like: varint(tag = (fieldNumber<<3) | wireKind)
//         // You may have your own byte writer helpers; placeholder uses
//         // out.bytes(...) only.
//         (void)fd;
//     }
//
//     // Similar write_* methods but using byte::WriteStream
//     bool ok() const { return out.ok(); }
//     ao::pack::Error error() const { return out.error(); }
// };
// template <class InStream>
// struct DiskDecodeCodec {
//     InStream& in;
//     DiskTables const& disk;
//
//     void msg_begin(uint32_t /*msgId*/) {}
//     void msg_end(uint32_t /*msgId*/) {}
//
//     void field_begin(uint32_t /*fieldId*/) {}
//     void field_end(uint32_t /*fieldId*/) {}
//
//     bool ok() const { return in.ok(); }
//     ao::pack::Error error() const { return in.error(); }
// };

}  // namespace ao::schema::vm
