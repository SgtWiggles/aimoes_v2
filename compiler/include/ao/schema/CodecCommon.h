#pragma once
#include <concepts>
#include <cstdint>
#include <vector>

#include "ao/pack/Error.h"
#include "ao/schema/IR.h"

namespace ao::schema::codec {

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
    std::vector<uint32_t> oneofFieldNumbers;

    std::vector<CodecField> fields;
};

struct CodecBytes {};
struct CodecBits {};

/**
 * @brief Concept for a Codec that handles encoding (serialization).
 */
template <typename T>
concept CodecEncode = requires(T codec,
                               uint32_t u32,
                               uint64_t u64,
                               int64_t i64,
                               float f,
                               double d,
                               bool b) {
    // Nested type requirement
    typename T::ChunkSize;

    // Lifecycle / Status
    { codec.ok() } -> std::convertible_to<bool>;
    { codec.error() } -> std::same_as<ao::pack::Error>;

    // Message & Field Framing
    codec.msgBegin(u32);
    codec.msgEnd();
    codec.fieldBegin(u32);
    codec.fieldEnd();
    codec.fieldId(u32);

    // Primitives
    codec.boolean(b);
    codec.u64(u32, u64);  // width, value
    codec.i64(u32, i64);  // width, value
    codec.f32(f);
    codec.f64(d);

    // Optionals
    codec.optBegin();
    codec.optEnd();
    codec.present(b);

    // Arrays
    codec.arrayBegin();
    codec.arrayEnd();
    codec.arrayLen(u32, u32);  // width, length

    // Oneofs
    codec.oneofEnter(u32);
    codec.oneofExit();
    codec.oneofArm(u32, u64);  // width, armId
};

/**
 * @brief Concept for a Codec that handles decoding (deserialization).
 */
template <typename T>
concept CodecDecode = requires(T codec, uint32_t u32) {
    // Nested type requirement
    typename T::ChunkSize;

    // Lifecycle / Status
    { codec.ok() } -> std::convertible_to<bool>;
    { codec.error() } -> std::same_as<ao::pack::Error>;

    // Message & Field Framing
    codec.msgBegin(u32);
    codec.msgEnd();
    codec.fieldBegin(u32);
    codec.fieldEnd();
    { codec.fieldId(u32) } -> std::same_as<bool>;
    { codec.skipFieldId(u32) } -> std::same_as<bool>;

    // Primitives
    { codec.boolean() } -> std::same_as<bool>;
    { codec.u64(u32) } -> std::same_as<uint64_t>;
    { codec.i64(u32) } -> std::same_as<int64_t>;
    { codec.f32() } -> std::same_as<float>;
    { codec.f64() } -> std::same_as<double>;

    // Optionals
    codec.optBegin();
    codec.optEnd();
    { codec.present() } -> std::same_as<bool>;

    // Arrays
    codec.arrayBegin();
    codec.arrayEnd();
    { codec.arrayLen(u32) } -> std::same_as<uint32_t>;

    // Oneofs
    codec.oneofEnter(u32);
    codec.oneofExit();
    { codec.oneofArm(u32, u32) } -> std::same_as<uint32_t>;  // oneofId, width
};

CodecTable generateCodecTable(ir::IR const& ir);

}  // namespace ao::schema::codec
