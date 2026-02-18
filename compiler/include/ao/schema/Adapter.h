#pragma once

#include <cstdint>
#include "VM.h"

namespace ao::schema {
struct Adapter {
    // Called by VM
    void msgBegin(uint16_t msgId);
    void msgEnd(uint16_t msgId);

    void fieldBegin(uint16_t fieldId);
    void fieldEnd(uint16_t fieldId);

    bool fieldPresent();   // uses current fieldId
    bool optPresent();     // uses current optional context
    void optEntryValue();  // encode only

    uint32_t arrLen();
    void arrEnterElem(uint32_t i);  // sets current element src/dst context
    void arrExitElem();

    // decode-select or encode-index depending on program
    int32_t oneofChoose();
    void oneofEnterArm(int32_t arm);  // prepare current value context for arm

    // Scalar IO. These operate on the adapter's current src/dst field pointers.

    // decode: writes directly to field storage
    void scalarRead(vm::ScalarKind k);

    void scalarGet(vm::ScalarKind k, uint64_t& reg);   // encode
    void scalarWrite(vm::ScalarKind k, uint64_t reg);  // encode

    // Disk/Net specific (encode/decode programs only emit these when needed)
    void fieldWriteTag(vm::TagKind);
    void fieldSkip();  // decode only
    void submsgBegin();
    void submsgEnd();
};
}  // namespace ao::schema
