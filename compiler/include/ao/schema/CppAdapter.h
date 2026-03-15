#pragma once
#include <cstdint>
#include <vector>

#include "ao/pack/Error.h"

namespace ao::schema::cpp {

struct AnyPtr {
    void const* ptr = nullptr;
    template <class T>
    T const& as() {
        return *static_cast<T const*>(ptr);
    }
};
struct MutPtr {
    void* ptr = nullptr;
    template <class T>
    T const& as() {
        return *static_cast<T*>(ptr);
    }
};

enum class FrameKind {
    Message,
    Field,
    Optional,
    OptionalValue,
    Array,
    ArrayElement,
    Oneof,
    OneofArm,
};

struct TypeOps;

template <class Ptr>
struct Frame {
    FrameKind kind;
    TypeOps const* ops;
    Ptr data;
};
using EncodeFrame = Frame<AnyPtr>;
using DecodeFrame = Frame<MutPtr>;

enum class TypeKind : uint8_t {
    Bool,
    Int,
    UInt,
    F32,
    F64,
    Char,
    Byte,
    Array,
    Optional,
    OneOf,
    Message,
};

struct TypeDesc {
    TypeKind kind = TypeKind::UInt;
    uint16_t width = 0;  // for scalars
    uint32_t aux = 0;    // Do we even need these?
};

struct CppEncodeRuntime {
    ao::pack::Error error = ao::pack::Error::Ok;
    std::vector<EncodeFrame> encodeStack;
};
struct CppDecodeRuntime {
    ao::pack::Error error = ao::pack::Error::Ok;
    std::vector<DecodeFrame> encodeStack;
};

// Dispatches to one of the above _or_ operates on scalars directly
struct EncodeTypeOps {
    using Ptr = AnyPtr;
    void (*msgBegin)(CppEncodeRuntime& runtime, AnyPtr ptr, uint32_t msgId);
    void (*msgEnd)(CppEncodeRuntime& runtime, AnyPtr ptr, uint32_t msgId);

    void (*fieldBegin)(CppEncodeRuntime& runtime, AnyPtr ptr, uint32_t fieldId);
    void (*fieldEnd)(CppEncodeRuntime& runtime, AnyPtr ptr);

    bool (*optionalHasValue)(CppEncodeRuntime& runtime, AnyPtr ptr);
    void (*optionalEnter)(CppEncodeRuntime& runtime, AnyPtr ptr);
    void (*optionalExit)(CppEncodeRuntime& runtime, AnyPtr ptr);
    void (*optionalEnterValue)(CppEncodeRuntime& runtime, AnyPtr ptr);
    void (*optionalExitValue)(CppEncodeRuntime& runtime, AnyPtr ptr);

    void (*arrayEnter)(CppEncodeRuntime& runtime, AnyPtr ptr, uint32_t typeId);
    void (*arrayExit)(CppEncodeRuntime& runtime, AnyPtr ptr);
    uint32_t (*arrayLen)(CppEncodeRuntime& runtime, AnyPtr ptr);
    void (*arrayEnterElem)(CppEncodeRuntime& runtime, AnyPtr ptr, uint32_t i);
    void (*arrayExitElem)(CppEncodeRuntime& runtime, AnyPtr ptr);

    uint32_t (*oneofIndex)(CppEncodeRuntime& runtime,
                           AnyPtr ptr,
                           uint32_t oneofId,
                           uint32_t width);
    void (*oneofEnter)(CppEncodeRuntime& runtime, AnyPtr ptr, uint32_t oneofId);
    void (*oneofExit)(CppEncodeRuntime& runtime, AnyPtr ptr);
    void (*oneofEnterArm)(CppEncodeRuntime& runtime,
                          uint32_t oneofId,
                          uint32_t armId);
    void (*oneofExitArm)(CppEncodeRuntime& runtime, AnyPtr ptr);

    bool (*boolean)(CppEncodeRuntime& runtime, AnyPtr ptr);
    uint64_t (*u64)(CppEncodeRuntime& runtime, AnyPtr ptr, uint16_t width);
    int64_t (*i64)(CppEncodeRuntime& runtime, AnyPtr ptr, uint16_t width);
    float (*f32)(CppEncodeRuntime& runtime, AnyPtr ptr);
    double (*f64)(CppEncodeRuntime& runtime, AnyPtr ptr);
};
struct DecodeTypeOps {
    void (*msgBegin)(CppDecodeRuntime& runtime, MutPtr ptr, uint32_t msgId);
    void (*msgEnd)(CppDecodeRuntime& runtime, MutPtr ptr);

    void (*fieldBegin)(CppDecodeRuntime& runtime, MutPtr ptr, uint32_t fieldId);
    void (*fieldEnd)(CppDecodeRuntime& runtime, MutPtr ptr);

    void (*optionalEnter)(CppDecodeRuntime& runtime, MutPtr ptr);
    void (*optionalExit)(CppDecodeRuntime& runtime, MutPtr ptr);
    void (*optionalSetPresent)(CppDecodeRuntime& runtime,
                               MutPtr ptr,
                               bool present);

    void (*arrayEnter)(CppDecodeRuntime& runtime, MutPtr ptr, uint32_t typeId);
    void (*arrayExit)(CppDecodeRuntime& runtime, MutPtr ptr);
    void (*arrayPrepare)(CppDecodeRuntime& runtime, MutPtr ptr, uint32_t len);
    void (*arrayEnterElem)(CppDecodeRuntime& runtime, MutPtr ptr, uint32_t i);
    void (*arrayExitElem)(CppDecodeRuntime& runtime, MutPtr ptr);

    void (*oneofEnter)(CppDecodeRuntime& runtime, MutPtr ptr, uint32_t oneofId);
    void (*oneofExit)(CppDecodeRuntime& runtime, MutPtr ptr);
    void (*oneofIndex)(CppDecodeRuntime& runtime,
                       MutPtr ptr,
                       uint32_t oneofId,
                       uint32_t armId);
    void (*oneofEnterArm)(CppDecodeRuntime& runtime,
                          MutPtr ptr,
                          uint32_t oneofId,
                          uint32_t armId);
    void (*oneofExitArm)(CppDecodeRuntime& runtime, MutPtr ptr);

    void (*boolean)(CppDecodeRuntime& runtime, MutPtr ptr, bool v);
    void (*u64)(CppDecodeRuntime& runtime,
                MutPtr ptr,
                uint16_t width,
                uint64_t v);
    void (*i64)(CppDecodeRuntime& runtime,
                MutPtr ptr,
                uint16_t width,
                int64_t v);
    void (*f32)(CppDecodeRuntime& runtime, MutPtr ptr, float v);
    void (*f64)(CppDecodeRuntime& runtime, MutPtr ptr, double v);
};

// Each type generates this
struct TypeOps {
    TypeDesc desc = {};
    EncodeTypeOps const* encodeOps = nullptr;
    DecodeTypeOps const* decodeOps = nullptr;
};

}  // namespace ao::schema::cpp
