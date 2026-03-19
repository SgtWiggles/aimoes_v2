#include "ao/schema/CppAdapter.h"

namespace ao::schema::cpp {
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

void CppEncodeAdapter::msgBegin(uint32_t msgId) {
    return stackInvoke(0, &EncodeTypeOps::msgBegin, msgId);
}
void CppEncodeAdapter::msgEnd() {
    return stackInvoke(0, &EncodeTypeOps::msgEnd);
}

void CppEncodeAdapter::fieldBegin(uint32_t fieldId) {
    return stackInvoke(0, &EncodeTypeOps::fieldBegin, fieldId);
}
void CppEncodeAdapter::fieldEnd() {
    return stackInvoke(1, &EncodeTypeOps::fieldEnd);
}

bool CppEncodeAdapter::optPresent() {
    return stackInvoke(0, &EncodeTypeOps::optionalHasValue);
}
void CppEncodeAdapter::optEnter() {
    return stackInvoke(0, &EncodeTypeOps::optionalEnter);
}
void CppEncodeAdapter::optExit() {
    return stackInvoke(0, &EncodeTypeOps::optionalExit);
}
void CppEncodeAdapter::optEnterValue() {
    // Expects the top frame to push a value into the stack
    return stackInvoke(0, &EncodeTypeOps::optionalEnterValue);
}
void CppEncodeAdapter::optExitValue() {
    return stackInvoke(1, &EncodeTypeOps::optionalExitValue);
}

void CppEncodeAdapter::arrayEnter(uint32_t typeId) {
    return stackInvoke(0, &EncodeTypeOps::arrayEnter, typeId);
}
void CppEncodeAdapter::arrayExit() {
    return stackInvoke(0, &EncodeTypeOps::arrayExit);
}
uint32_t CppEncodeAdapter::arrayLen() {
    return stackInvoke(0, &EncodeTypeOps::arrayLen);
}
void CppEncodeAdapter::arrayEnterElem(uint32_t i) {
    return stackInvoke(0, &EncodeTypeOps::arrayEnterElem, i);
}
void CppEncodeAdapter::arrayExitElem() {
    return stackInvoke(1, &EncodeTypeOps::arrayExitElem);
}

// chosen arm index (or -1)
uint32_t CppEncodeAdapter::oneofIndex(uint32_t oneofId, uint32_t width) {
    return stackInvoke(0, &EncodeTypeOps::oneofIndex, oneofId, width);
}

void CppEncodeAdapter::oneofEnter(uint32_t oneofId) {
    return stackInvoke(0, &EncodeTypeOps::oneofEnter, oneofId);
}
void CppEncodeAdapter::oneofExit() {
    return stackInvoke(0, &EncodeTypeOps::oneofExit);
}
void CppEncodeAdapter::oneofEnterArm(uint32_t oneofId, uint32_t armId) {
    return stackInvoke(0, &EncodeTypeOps::oneofEnterArm, oneofId, armId);
}
void CppEncodeAdapter::oneofExitArm() {
    return stackInvoke(1, &EncodeTypeOps::oneofExitArm);
}
bool CppEncodeAdapter::boolean() {
    return stackInvoke(0, &EncodeTypeOps::boolean);
}
uint64_t CppEncodeAdapter::u64(uint16_t width) {
    return stackInvoke(0, &EncodeTypeOps::u64, width);
}
int64_t CppEncodeAdapter::i64(uint16_t width) {
    return stackInvoke(0, &EncodeTypeOps::i64, width);
}
float CppEncodeAdapter::f32() {
    return stackInvoke(0, &EncodeTypeOps::f32);
}
double CppEncodeAdapter::f64() {
    return stackInvoke(0, &EncodeTypeOps::f64);
}

bool CppEncodeAdapter::require(bool condition) {
    if (!ok())
        return false;
    cppRuntimeFail(m_runtime, condition ? error() : ao::pack::Error::BadData);
    return ok();
}

EncodeFrame* CppEncodeAdapter::stackBack(size_t offset) {
    if (!ok())
        return nullptr;

    if (m_runtime.stack.size() <= offset + 1) {
        cppRuntimeFail(m_runtime, ao::pack::Error::BadData);
        return nullptr;
    }
    return &m_runtime.stack[m_runtime.stack.size() - (offset + 1)];
}

// Message navigation:
void CppDecodeAdapter::msgBegin(uint32_t msgId) {
    return stackInvoke(0, &DecodeTypeOps::msgBegin, msgId);
}
void CppDecodeAdapter::msgEnd() {
    return stackInvoke(0, &DecodeTypeOps::msgEnd);
}

// Field navigation:
void CppDecodeAdapter::fieldBegin(uint32_t fieldId) {
    return stackInvoke(0, &DecodeTypeOps::fieldBegin, fieldId);
}
void CppDecodeAdapter::fieldEnd() {
    return stackInvoke(1, &DecodeTypeOps::fieldEnd);
}

// Optional:
// For optional decode, codec typically reads "present bit" and VM branches{}
// object adapter must allocate/set the optional storage before decoding
// inner value.
// prepare optional storage (e.g., emplace or reset)
void CppDecodeAdapter::optEnter() {
    return stackInvoke(0, &DecodeTypeOps::optionalEnter);
}
void CppDecodeAdapter::optExit() {
    return stackInvoke(0, &DecodeTypeOps::optionalExit);
}

void CppDecodeAdapter::optSetPresent(bool present) {
    return stackInvoke(0, &DecodeTypeOps::optionalSetPresent, present);
}
// enter optional's value storage (must be present)

void CppDecodeAdapter::optEnterValue() {
    return stackInvoke(0, &DecodeTypeOps::optionalEnterValue);
}
void CppDecodeAdapter::optExitValue() {
    return stackInvoke(1, &DecodeTypeOps::optionalExitValue);
}

// Array:
// For decode, codec provides length{} object adapter must resize/prepare
// container.
void CppDecodeAdapter::arrayEnter(uint32_t typeId) {
    return stackInvoke(0, &DecodeTypeOps::arrayEnter, typeId);
}
void CppDecodeAdapter::arrayExit() {
    return stackInvoke(0, &DecodeTypeOps::arrayExit);
}
void CppDecodeAdapter::arrayPrepare(uint32_t len) {
    return stackInvoke(0, &DecodeTypeOps::arrayPrepare, len);
}
void CppDecodeAdapter::arrayEnterElem(uint32_t i) {
    return stackInvoke(0, &DecodeTypeOps::arrayEnterElem, i);
}
void CppDecodeAdapter::arrayExitElem() {
    return stackInvoke(1, &DecodeTypeOps::arrayExitElem);
}

// Oneof:
// For decode, codec selects arm{} object adapter must set discriminant and
// prepare arm storage.
void CppDecodeAdapter::oneofEnter(uint32_t oneofId) {
    return stackInvoke(0, &DecodeTypeOps::oneofEnter, oneofId);
}
void CppDecodeAdapter::oneofExit() {
    return stackInvoke(0, &DecodeTypeOps::oneofExit);
}
void CppDecodeAdapter::oneofIndex(uint32_t oneofId, uint32_t armId) {
    return stackInvoke(0, &DecodeTypeOps::oneofIndex, oneofId, armId);
}

void CppDecodeAdapter::oneofEnterArm(uint32_t oneofId, uint32_t armId) {
    return stackInvoke(0, &DecodeTypeOps::oneofEnterArm, oneofId, armId);
}
void CppDecodeAdapter::oneofExitArm() {
    return stackInvoke(1, &DecodeTypeOps::oneofExitArm);
}

// Scalars (write into current storage):
void CppDecodeAdapter::boolean(bool v) {
    return stackInvoke(0, &DecodeTypeOps::boolean, v);
}
void CppDecodeAdapter::u64(uint16_t width, uint64_t v) {
    return stackInvoke(0, &DecodeTypeOps::u64, width, v);
}
void CppDecodeAdapter::i64(uint16_t width, int64_t v) {
    return stackInvoke(0, &DecodeTypeOps::i64, width, v);
}
void CppDecodeAdapter::f32(float v) {
    return stackInvoke(0, &DecodeTypeOps::f32, v);
}
void CppDecodeAdapter::f64(double v) {
    return stackInvoke(0, &DecodeTypeOps::f64, v);
}
bool CppDecodeAdapter::require(bool condition) {
    if (!ok())
        return false;
    cppRuntimeFail(m_runtime, condition ? error() : ao::pack::Error::BadData);
    return ok();
}

DecodeFrame* CppDecodeAdapter::stackBack(size_t offset) {
    if (!ok())
        return nullptr;

    if (m_runtime.stack.size() <= offset + 1) {
        cppRuntimeFail(m_runtime, ao::pack::Error::BadData);
        return nullptr;
    }
    return &m_runtime.stack[m_runtime.stack.size() - (offset + 1)];
}

}  // namespace ao::schema::cpp
