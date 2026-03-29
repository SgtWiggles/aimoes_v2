#pragma once
#include <cstdint>
#include <variant>
#include <vector>

#include "Error.h"
#include "VM.h"

namespace ao::schema::vm {
// Each type generates to an instruction so start there?
// Then link everything together
// Then resolve all of the labels

struct Label {
    bool generated;
    uint64_t loc;
};

struct FixUpInstr {
    Instr instr;
    ExtKind ext;  // in case there's far jumps
    uint64_t label;
};
struct FixUp32 {
    uint64_t label;
    // extra offset to add to the final jump
    // used when this instruction might not be the base of the jump
    // JMP32, JZ32 and DISPATCH use it
    int64_t offset = 0;
};

struct Entry {
    std::variant<Instr, FixUpInstr, FixUp32> instr;
    std::optional<uint64_t> label;  // label defined at this location
};

struct Assembler {
    void emit(Instr instr, std::optional<uint64_t> label);
    void emitFixup(Instr instr,
                   ExtKind ext,
                   uint64_t destLabel,
                   std::optional<uint64_t> label);
    void emitDispatch(std::vector<uint64_t> dispatchLabels,
                      uint64_t failLabel,
                      std::optional<uint64_t> label);
    void jz(uint64_t destLabel, std::optional<uint64_t> label) {
        emitFixup({Op::JZ, 0, 0}, ExtKind::JZ32, destLabel, label);
    }
    void jmp(uint64_t destLabel, std::optional<uint64_t> label) {
        emitFixup({Op::JMP, 0, 0}, ExtKind::JMP32, destLabel, label);
    }

    void emitTypeCall(IdFor<ir::Type> type, std::optional<uint64_t> label) {
        emitExt32(Op::CALL_TYPE, ExtKind::CALL_TYPE32, type.idx, label);
    }
    void emitFieldBegin(IdFor<ir::Field> type, std::optional<uint64_t> label) {
        emitExt32(Op::FIELD_BEGIN, ExtKind::FIELD_BEGIN32, type.idx, label);
    }
    void emitMsgBegin(IdFor<ir::Message> msg, std::optional<uint64_t> label) {
        emitExt32(Op::MSG_BEGIN, ExtKind::MSG_BEGIN32, msg.idx, label);
    }
    void arrayBegin(uint64_t typeId, std::optional<uint64_t> label) {
         emitExt32(Op::ARRAY_BEGIN, ExtKind::ARRAY_BEGIN32, typeId, label);
    }
    void oneofBegin(uint64_t oneofId, std::optional<uint64_t> label) {
        emitExt32(Op::ONEOF_BEGIN, ExtKind::ONEOF_BEGIN32, oneofId, label);
    }

    void emitExt32(Op baseOp,
                   ExtKind ext,
                   uint64_t idx,
                   std::optional<uint64_t> label);
    uint64_t useLabel();
    std::vector<uint32_t> assemble(ErrorContext& err) const;

    std::vector<Entry> instructions;
    std::vector<Label> labels;
};

template <class T, class U>
bool inRange(T min, U v, T max) {
    return min <= v && v <= max;
}
template <class T, class U>
bool fitsIn(U v) {
    return inRange(std::numeric_limits<T>::min(), v,
                   std::numeric_limits<T>::max());
}
}  // namespace ao::schema::vm
