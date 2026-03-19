#pragma once
#include <cstdint>
#include <vector>

#include "ao/pack/Error.h"
#include "ao/schema/VM.h"

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

template <class Ops, class Ptr>
struct Frame {
    Ops const* ops;
    Ptr data;
};

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
struct CppEncodeRuntime;
struct CppDecodeRuntime;

// Dispatches to one of the above _or_ operates on scalars directly
struct EncodeTypeOps {
    using Ptr = AnyPtr;
    void (*msgBegin)(CppEncodeRuntime& runtime,
                     AnyPtr ptr,
                     uint32_t msgId) = nullptr;
    void (*msgEnd)(CppEncodeRuntime& runtime, AnyPtr ptr) = nullptr;

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

using EncodeFrame = Frame<EncodeTypeOps, AnyPtr>;
using DecodeFrame = Frame<DecodeTypeOps, MutPtr>;

struct CppEncodeRuntime {
    ao::pack::Error error = ao::pack::Error::Ok;
    std::vector<EncodeFrame> stack;
};
struct CppDecodeRuntime {
    ao::pack::Error error = ao::pack::Error::Ok;
    std::vector<DecodeFrame> stack;
};

void cppRuntimeFail(CppEncodeRuntime& runtime, ao::pack::Error err);
void cppRuntimeFail(CppDecodeRuntime& runtime, ao::pack::Error err);

class CppEncodeAdapter {
   public:
    void msgBegin(uint32_t msgId);
    void msgEnd();

    void fieldBegin(uint32_t fieldId);
    void fieldEnd();

    bool optPresent();
    void optEnter();
    void optExit();
    void optEnterValue();
    void optExitValue();

    void arrayEnter(uint32_t typeId);
    void arrayExit();
    uint32_t arrayLen();
    void arrayEnterElem(uint32_t i);
    void arrayExitElem();

    // chosen arm index (or -1)
    uint32_t oneofIndex(uint32_t oneofId, uint32_t width);

    void oneofEnter(uint32_t oneofId);
    void oneofExit();
    void oneofEnterArm(uint32_t oneofId, uint32_t armId);
    void oneofExitArm();

    bool boolean();
    uint64_t u64(uint16_t width);
    int64_t i64(uint16_t width);
    float f32();
    double f64();

    bool ok() const { return m_runtime.error == pack::Error::Ok; }
    ao::pack::Error error() const { return m_runtime.error; }

    template <class T>
    void setRoot(T& data) {
        m_runtime.error = ao::pack::Error::Ok;
        m_runtime.stack.clear();
        m_runtime.stack.emplace_back({
            .ops = &T::Accessor::encode,
            .data = AnyPtr{data},
        });
    }

   private:
    bool require(bool condition);
    EncodeFrame* stackBack(size_t offset);
    template <class InvokeType, class InvokeMember, class... Args>
    auto stackInvoke(size_t offset,
                     InvokeType InvokeMember::* member,
                     Args&&... args) {
        auto frame = stackBack(offset);
        using Ret = decltype((frame->ops->*member)(
            m_runtime, frame->data, std::forward<Args>(args)...));

        if (!frame)
            return Ret{};
        auto fnPtr = frame->ops->*member;
        if (!require(fnPtr != nullptr && frame->data.ptr != nullptr))
            return Ret{};
        return (fnPtr)(m_runtime, frame->data, std::forward<Args>(args)...);
    }
    CppEncodeRuntime m_runtime;
};

class CppDecodeAdapter {
   public:
    // Message navigation:
    void msgBegin(uint32_t msgId);
    void msgEnd();

    // Field navigation:
    void fieldBegin(uint32_t fieldId);
    void fieldEnd();

    // Optional:
    // For optional decode, codec typically reads "present bit" and VM branches;
    // object adapter must allocate/set the optional storage before decoding
    // inner value.
    // prepare optional storage (e.g., emplace or reset)
    void optEnter();
    void optExit();

    void optSetPresent(bool present);
    // enter optional's value storage (must be present)

    void optEnterValue();
    void optExitValue();

    // Array:
    // For decode, codec provides length; object adapter must resize/prepare
    // container.
    void arrayEnter(uint32_t typeId);
    void arrayExit();
    void arrayPrepare(uint32_t len);
    void arrayEnterElem(uint32_t i);
    void arrayExitElem();

    // Oneof:
    // For decode, codec selects arm; object adapter must set discriminant and
    // prepare arm storage.
    void oneofEnter(uint32_t oneofId);
    void oneofExit();
    void oneofIndex(uint32_t oneofId, uint32_t armId);

    void oneofEnterArm(uint32_t oneofId, uint32_t armId);
    void oneofExitArm();

    // Scalars (write into current storage):
    void boolean(bool v);
    void u64(uint16_t width, uint64_t v);
    void i64(uint16_t width, int64_t v);
    void f32(float v);
    void f64(double v);

    // Status:
    bool ok() const { return m_runtime.error == pack::Error::Ok; }
    ao::pack::Error error() const { return m_runtime.error; }

    template <class T>
    void setRoot(T& data) {
        m_runtime.error = ao::pack::Error::Ok;
        m_runtime.stack.clear();
        m_runtime.stack.emplace_back({
            .ops = &T::Accessor::decode,
            .data = MutPtr{data},
        });
    }

   private:
    bool require(bool condition);
    DecodeFrame* stackBack(size_t offset);
    template <class InvokeType, class InvokeMember, class... Args>
    auto stackInvoke(size_t offset,
                     InvokeType InvokeMember::* member,
                     Args&&... args) {
        auto frame = stackBack(offset);
        using Ret = decltype((frame->ops->*member)(
            m_runtime, frame->data, std::forward<Args>(args)...));

        if (!frame)
            return Ret{};
        auto fnPtr = frame->ops->*member;
        if (!require(fnPtr != nullptr && frame->data.ptr != nullptr))
            return Ret{};
        return (fnPtr)(m_runtime, frame->data, std::forward<Args>(args)...);
    }
    CppDecodeRuntime m_runtime;
};
}  // namespace ao::schema::cpp
