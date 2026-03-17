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
    T& as() {
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
    void (*msgBegin)(CppEncodeRuntime& runtime,
                     AnyPtr ptr,
                     uint32_t msgId) = nullptr;
    void (*msgEnd)(CppEncodeRuntime& runtime,
                   AnyPtr ptr,
                   uint32_t msgId) = nullptr;

    void (*fieldBegin)(CppEncodeRuntime& runtime,
                       AnyPtr ptr,
                       uint32_t fieldId) = nullptr;
    void (*fieldEnd)(CppEncodeRuntime& runtime, AnyPtr ptr) = nullptr;

    bool (*optionalHasValue)(CppEncodeRuntime& runtime, AnyPtr ptr) = nullptr;
    void (*optionalEnter)(CppEncodeRuntime& runtime, AnyPtr ptr) = nullptr;
    void (*optionalExit)(CppEncodeRuntime& runtime, AnyPtr ptr) = nullptr;
    void (*optionalEnterValue)(CppEncodeRuntime& runtime, AnyPtr ptr) = nullptr;
    void (*optionalExitValue)(CppEncodeRuntime& runtime, AnyPtr ptr) = nullptr;

    void (*arrayEnter)(CppEncodeRuntime& runtime,
                       AnyPtr ptr,
                       uint32_t typeId) = nullptr;
    void (*arrayExit)(CppEncodeRuntime& runtime, AnyPtr ptr) = nullptr;
    uint32_t (*arrayLen)(CppEncodeRuntime& runtime, AnyPtr ptr) = nullptr;
    void (*arrayEnterElem)(CppEncodeRuntime& runtime,
                           AnyPtr ptr,
                           uint32_t i) = nullptr;
    void (*arrayExitElem)(CppEncodeRuntime& runtime, AnyPtr ptr) = nullptr;

    uint32_t (*oneofIndex)(CppEncodeRuntime& runtime,
                           AnyPtr ptr,
                           uint32_t oneofId,
                           uint32_t width) = nullptr;
    void (*oneofEnter)(CppEncodeRuntime& runtime, AnyPtr ptr, uint32_t oneofId);
    void (*oneofExit)(CppEncodeRuntime& runtime, AnyPtr ptr) = nullptr;
    void (*oneofEnterArm)(CppEncodeRuntime& runtime,
                          AnyPtr ptr,
                          uint32_t oneofId,
                          uint32_t armId) = nullptr;
    void (*oneofExitArm)(CppEncodeRuntime& runtime, AnyPtr ptr) = nullptr;

    bool (*boolean)(CppEncodeRuntime& runtime, AnyPtr ptr) = nullptr;
    uint64_t (*u64)(CppEncodeRuntime& runtime,
                    AnyPtr ptr,
                    uint16_t width) = nullptr;
    int64_t (*i64)(CppEncodeRuntime& runtime,
                   AnyPtr ptr,
                   uint16_t width) = nullptr;
    float (*f32)(CppEncodeRuntime& runtime, AnyPtr ptr) = nullptr;
    double (*f64)(CppEncodeRuntime& runtime, AnyPtr ptr) = nullptr;
};
struct DecodeTypeOps {
    void (*msgBegin)(CppDecodeRuntime& runtime,
                     MutPtr ptr,
                     uint32_t msgId) = nullptr;
    void (*msgEnd)(CppDecodeRuntime& runtime, MutPtr ptr) = nullptr;

    void (*fieldBegin)(CppDecodeRuntime& runtime,
                       MutPtr ptr,
                       uint32_t fieldId) = nullptr;
    void (*fieldEnd)(CppDecodeRuntime& runtime, MutPtr ptr) = nullptr;

    void (*optionalEnter)(CppDecodeRuntime& runtime, MutPtr ptr) = nullptr;
    void (*optionalExit)(CppDecodeRuntime& runtime, MutPtr ptr) = nullptr;
    void (*optionalSetPresent)(CppDecodeRuntime& runtime,
                               MutPtr ptr,
                               bool present) = nullptr;
    void (*optionalEnterValue)(CppDecodeRuntime& runtime, MutPtr ptr) = nullptr;
    void (*optionalExitValue)(CppDecodeRuntime& runtime, MutPtr ptr) = nullptr;

    void (*arrayEnter)(CppDecodeRuntime& runtime,
                       MutPtr ptr,
                       uint32_t typeId) = nullptr;
    void (*arrayExit)(CppDecodeRuntime& runtime, MutPtr ptr) = nullptr;
    void (*arrayPrepare)(CppDecodeRuntime& runtime,
                         MutPtr ptr,
                         uint32_t len) = nullptr;
    void (*arrayEnterElem)(CppDecodeRuntime& runtime,
                           MutPtr ptr,
                           uint32_t i) = nullptr;
    void (*arrayExitElem)(CppDecodeRuntime& runtime, MutPtr ptr) = nullptr;

    void (*oneofEnter)(CppDecodeRuntime& runtime,
                       MutPtr ptr,
                       uint32_t oneofId) = nullptr;
    void (*oneofExit)(CppDecodeRuntime& runtime, MutPtr ptr) = nullptr;
    void (*oneofIndex)(CppDecodeRuntime& runtime,
                       MutPtr ptr,
                       uint32_t oneofId,
                       uint32_t armId) = nullptr;
    void (*oneofEnterArm)(CppDecodeRuntime& runtime,
                          MutPtr ptr,
                          uint32_t oneofId,
                          uint32_t armId) = nullptr;
    void (*oneofExitArm)(CppDecodeRuntime& runtime, MutPtr ptr) = nullptr;

    void (*boolean)(CppDecodeRuntime& runtime, MutPtr ptr, bool v) = nullptr;
    void (*u64)(CppDecodeRuntime& runtime,
                MutPtr ptr,
                uint16_t width,
                uint64_t v) = nullptr;
    void (*i64)(CppDecodeRuntime& runtime,
                MutPtr ptr,
                uint16_t width,
                int64_t v) = nullptr;
    void (*f32)(CppDecodeRuntime& runtime, MutPtr ptr, float v) = nullptr;
    void (*f64)(CppDecodeRuntime& runtime, MutPtr ptr, double v) = nullptr;
};

// Each type generates this
struct TypeOps {
    TypeDesc desc = {};
    EncodeTypeOps const* encodeOps = nullptr;
    DecodeTypeOps const* decodeOps = nullptr;
};

template <size_t T>
struct EncodeAccessor;

template <size_t T>
struct DecodeAccessor;

void cppRuntimeFail(CppEncodeRuntime& runtime, ao::pack::Error err) {
    if (runtime.error != ao::pack::Error::Ok)
        return;
    runtime.error = err;
}
void cppRuntimeFail(CppDecodeRuntime& runtime, ao::pack::Error err) {
    if (runtime.error != ao::pack::Error::Ok)
        return;
    runtime.error = err;
}

}  // namespace ao::schema::cpp
