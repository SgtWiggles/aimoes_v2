#pragma once
#include <compare>
#include <cstdint>
#include <vector>

#include <nlohmann/json.hpp>

#include "ao/schema/Codec.h"
#include "ao/schema/IR.h"

namespace ao::schema::vm {

// ------------------------------------------------------------
// Small enums
// ------------------------------------------------------------
using ScalarKind = ao::schema::ir::Scalar::ScalarKind;

enum class ExtKind : uint8_t {
    // Jumps relative to EXT32 instruction
    JMP32,          // imm32: rel32
    JZ32,           // imm32: rel32
    CALL32,         // imm32: rel32
    MSG_BEGIN32,    // imm32: msgId
    FIELD_BEGIN32,  // imm32: fieldId
    CALL_TYPE32,    // imm32: typeEntryId
    DISPATCH32,     // imm32: dispatch
};

enum class JumpTableKind : uint8_t {
    Rel16 = 0,
    Rel32 = 1,
};

enum class TagKind : uint8_t {
    Default = 0,
    Packed = 1,
};

enum BeginFlags : uint8_t {
    None = 0,
    PreferPacked = 1 << 0,  // arrays
    HasTagField = 1 << 1,   // oneof
};

// ------------------------------------------------------------
// Opcode map (v2)
// ------------------------------------------------------------
enum class Op : uint8_t {
    HALT,
    // Stop execution

    JMP,
    // imm16: rel16

    JZ,
    // imm16: rel16

    CALL,
    // imm16: rel16

    RET,
    // pop return pc

    EXT32,
    // a: ExtKind
    // Next word is imm32 payload interpreted according to a.

    CALL_TYPE,
    // imm16: type id

    CALL_TYPE_INDIRECT,
    // vm.reg: type id
    // Jumps to the type id stored in vm.reg

    DISPATCH,
    // a: register
    // imm16: branch count
    // imm32: 32 bit rel jumps for each dispatch
    // imm32: 32 bit rel jump for out of bounds case
    // All jumps are relative to the root dispatch call

    // Stack frame functions
    MSG_BEGIN,
    // stack.push(MsgFrame)
    // adapter.msgBegin()
    // codec.msgBegin()

    MSG_END,
    // stack.pop(MsgFrame)
    // adapter.msgEnd()
    // codec.msgEnd()

    FIELD_BEGIN,
    // imm16: field id
    // adapter.fieldBegin(imm16)
    // codec.fieldBegin(imm16)
    FIELD_END,
    // adapter.fieldEnd()
    // codec.fieldEnd()

    OPT_BEGIN,
    OPT_END,
    // adapter.optBegin()
    // codec.optBegin()
    OPT_BEGIN_VALUE,
    OPT_END_VALUE,

    ONEOF_BEGIN,
    ONEOF_END,
    // Run the entry functions for both

    ONEOF_ARM_BEGIN,
    ONEOF_ARM_END,

    ARRAY_BEGIN,
    ARRAY_END,
    // Run the entry functions for both
    // stack.push(array_frame)
    // stack.array.index = -1

    ARRAY_ELEM_BEGIN,
    ARRAY_ELEM_END,
    // Run the entry functions for both
    // Passes in the index

    ARRAY_NEXT,
    // stack.array_index += 1
    // a = 1 if index is in bounds

    ENVELOPE_BEGIN,
    ENVELOPE_END,

    // Codec functions

    // Top level message framing
    // Disk format these are standard TLV using message numbers
    // Net format just the message id

    C_WRITE_FIELD_ID,
    // Write the current field id, idempotent for net format
    // adapter.writeFieldNumber(reg)

    C_MATCH_FIELD_ID,
    // net always returns true, disk does matching
    // imm16: expected field id
    // flag = expected == current field id

    C_SKIP_FIELD_ID,
    // always returns false for net format
    // imm16: original expected field id
    // flag = field was skipped
    // codec.skip_field

    C_WRITE_SCALAR,
    // Switch on scalar type and write it
    C_READ_SCALAR,
    // reg = Read bits from scalar type

    C_WRITE_OPT_PRESENT,
    // codec.writeOptPresent(flag)
    C_READ_OPT_PRESENT,
    // flag = opt is present

    C_WRITE_ONEOF_ARM,
    // codec.writeOneOfArm(reg)
    C_READ_ONEOF_ARM,
    // case = armid of the current case

    C_WRITE_ARRAY_LEN,
    // codec.writeArrayLen(reg)
    C_READ_ARRAY_LEN,
    // reg = len of array

    // Disk codec functions

    // Object Functions
    O_WRITE_SCALAR,
    // Switch on scalar type and write it
    O_READ_SCALAR,
    // reg = Read bits from scalar type

    O_WRITE_OPT_PRESENT,
    // adapter.write_opt_present(flag)
    O_READ_OPT_PRESENT,
    // flag = opt is present

    O_WRITE_ONEOF_ARM,
    // adapter.oneofEnterArm(reg)
    O_READ_ONEOF_ARM,
    // reg = adapter.oneofChoose()

    O_WRITE_ARRAY_LEN,
    // adapter.arrLen(reg)
    O_READ_ARRAY_LEN,
    // reg = adapter.arrLen()
};

struct Instr {
    Instr() = default;
    Instr(Op opcode, uint8_t mode, uint16_t imm)
        : op(opcode), mode(mode), imm(imm) {}

    Op op = Op::HALT;
    uint8_t mode = 0;
    uint16_t imm = 0;

    uint32_t pack() {
        return {
            static_cast<uint32_t>(op) | (static_cast<uint32_t>(mode) << 8) |
                (static_cast<uint32_t>(imm) << 16),
        };
    }

    auto operator<=>(Instr const& other) const = default;
};
inline Instr decodeInstr(uint32_t instr) {
    return {
        static_cast<Op>(instr & 0xFF),
        static_cast<uint8_t>((instr >> 8) & 0xFF),
        static_cast<uint16_t>((instr >> 16) & 0xFFFF),
    };
}

struct FieldDesc {
    uint32_t fieldNumber;
    uint32_t flags;
    uint32_t aux;
    uint32_t tagId;
};
static_assert(sizeof(FieldDesc) == 16);

struct JumpTableMeta {
    JumpTableKind kind;
    uint16_t armCount;
    uint32_t tableOffsetWords;
};

struct MessageIndex {
    std::unordered_map<std::string, uint64_t> messageNameToId;
    std::unordered_map<size_t, uint64_t> messageNumberToId;

    std::optional<uint64_t> getId(std::string const& qualifiedName) const {
        if (!messageNameToId.contains(qualifiedName))
            return {};
        return messageNameToId.at(qualifiedName);
    }
    std::optional<uint64_t> getId(size_t messageNumber) const {
        if (!messageNumberToId.contains(messageNumber))
            return {};
        return messageNumberToId.at(messageNumber);
    }
};

struct Program {
    std::vector<uint32_t> codeWords;
    std::vector<uint32_t> typeEntryPc;
    std::vector<uint32_t> msgEntryPc;
};

struct Format {
    Program encode;
    Program decode;
    MessageIndex msgs;
};

Format generateProgram(ao::schema::ir::IR const& irCode, ErrorContext& errs);

enum class VMError {
    Ok,
    InvalidProgram,
    RuntimeError,
    InvalidType,
    InvalidInstr,
    StackUnderflow,
    StackOverflow,
    ObjectError,
    CodecError,
};

struct CallFrame {
    uint32_t retPc;
};
struct ArrayFrame {
    uint32_t len;
    uint32_t idx;
};
struct OptionalFrame {
    uint32_t _reserved = 0;
};
struct OneofFrame {
    uint32_t oneofId = 0;
};

template <class ObjectAdapter, class CodecAdapter>
struct VM {
    static constexpr bool IsBitCodec =
        std::is_same_v<typename CodecAdapter::ChunkSize, codec::CodecBits>;
    Program const* prog = nullptr;
    ObjectAdapter object;
    CodecAdapter codec;

    uint32_t pc = 0;
    uint8_t flag = 0;
    int32_t oneofArm = -1;
    uint64_t reg = 0;

    uint8_t* dstBase = nullptr;
    uint8_t const* srcBase = nullptr;

    size_t stackDepth = 0;
    std::vector<CallFrame> callStack;
    std::vector<ArrayFrame> arrayStack;
    std::vector<OptionalFrame> optionalStack;
    std::vector<OneofFrame> oneofStack;

    VMError error;
};

struct VMSettings {
    size_t maxSteps = 10000;
    size_t maxRecursionDepth = 64;
    size_t maxArraySize = 1024;
};

namespace detail {
template <class VM>
void reset(VM& vm) {
    vm.pc = 0;
    vm.flag = 0;
    vm.oneofArm = -1;
    vm.reg = 0;
    vm.dstBase = nullptr;
    vm.srcBase = nullptr;
    vm.stackDepth = 0;
    vm.error = VMError::Ok;
}

template <class VM, class Object>
bool writeScalar(Instr instr, VM& vm, Object& o) {
    switch (instr.mode) {
        case ScalarKind::BOOL:
            o.boolean(vm.reg != 0);
            break;
        case ScalarKind::UINT:
            o.u64(instr.imm, vm.reg);
            break;
        case ScalarKind::INT:
            o.i64(instr.imm, vm.reg);
            break;
        case ScalarKind::F32: {
            auto tmp = (uint32_t)vm.reg;
            o.f32(std::bit_cast<float>(tmp));
        } break;
        case ScalarKind::F64:
            o.f64(std::bit_cast<double>(vm.reg));
            break;
        default:
            vm.error = VMError::InvalidInstr;
            return false;
    }
    return true;
}
template <class VM, class Object>
bool readScalar(Instr instr, VM& vm, Object& o) {
    switch (instr.mode) {
        case ScalarKind::BOOL:
            vm.reg = o.boolean();
            break;
        case ScalarKind::UINT:
            vm.reg = o.u64(instr.imm);
            break;
        case ScalarKind::INT:
            vm.reg = std::bit_cast<uint64_t>(o.i64(instr.imm));
            break;
        case ScalarKind::F32:
            vm.reg = std::bit_cast<uint32_t>(o.f32());
            break;
        case ScalarKind::F64:
            vm.reg = std::bit_cast<uint64_t>(o.f64());
            break;
        default:
            vm.error = VMError::InvalidInstr;
            return false;
    }
    return true;
}

template <bool EncodeMode, class VM>
bool runInstr(VM& vm) {
    if (vm.pc >= vm.prog->codeWords.size()) {
        vm.error = VMError::RuntimeError;
        return false;
    }
    auto instr = decodeInstr(vm.prog->codeWords[vm.pc]);
    auto nextPc = vm.pc + 1;

    switch (instr.op) {
        case Op::HALT:
            // Break from the program
            return false;
        case Op::JMP:
            nextPc = vm.pc + static_cast<int16_t>(instr.imm);
            break;
        case Op::JZ:
            if (vm.flag == 0)
                nextPc = vm.pc + static_cast<int16_t>(instr.imm);
            break;

        case Op::RET:
            if (vm.callStack.empty()) {
                vm.error = VMError::StackUnderflow;
                return false;
            }
            vm.stackDepth -= 1;
            nextPc = vm.callStack.back().retPc;
            vm.callStack.pop_back();
            // Pop call stack
            break;
        // case Op::EXT32:
        //     // TODO ext32 ops
        //     break;
        case Op::CALL_TYPE: {
            vm.stackDepth += 1;
            vm.callStack.emplace_back(CallFrame{
                .retPc = nextPc,
            });
            nextPc = vm.prog->typeEntryPc[instr.imm];
        } break;
        case Op::CALL_TYPE_INDIRECT: {
            vm.stackDepth += 1;
            vm.callStack.emplace_back(CallFrame{
                .retPc = nextPc,
            });
            if (vm.reg >= vm.prog->typeEntryPc.size()) {
                vm.error = VMError::RuntimeError;
                return false;
            }
            nextPc = vm.prog->typeEntryPc[vm.reg];
        } break;
        case Op::DISPATCH: {
            auto pc = vm.pc;
            pc += std::min(vm.reg + 1, static_cast<uint64_t>(instr.imm));
            if (pc >= vm.prog->codeWords.size())
                return (vm.error = VMError::RuntimeError, false);
            nextPc = vm.pc + vm.prog->codeWords[pc];
        } break;
        case Op::MSG_BEGIN: {
            vm.object.msgBegin(instr.imm);
            vm.codec.msgBegin(instr.imm);
        } break;
        case Op::MSG_END:
            vm.object.msgEnd();
            vm.codec.msgEnd();
            break;
        case Op::FIELD_BEGIN:
            vm.object.fieldBegin(instr.imm);
            vm.codec.fieldBegin(instr.imm);
            break;
        case Op::FIELD_END:
            vm.object.fieldEnd();
            vm.codec.fieldEnd();
            break;
        case Op::OPT_BEGIN: {
            // Maybe this is a no op?
            vm.object.optEnter();
            vm.codec.optBegin();
            vm.optionalStack.emplace_back(OptionalFrame{});
        } break;
        case Op::OPT_END: {
            // Maybe this is a no op?
            vm.object.optExit();
            vm.codec.optEnd();
            vm.optionalStack.pop_back();
        } break;
        case Op::OPT_BEGIN_VALUE: {
            vm.object.optEnterValue();
        } break;
        case Op::OPT_END_VALUE: {
            vm.object.optExitValue();
        } break;
        case Op::ONEOF_BEGIN: {
            // These might also be a nullopt
            vm.oneofStack.emplace_back(OneofFrame{(uint32_t)instr.imm});
            vm.object.oneofEnter(vm.oneofStack.back().oneofId);
            vm.codec.oneofEnter(vm.oneofStack.back().oneofId);
        } break;
        case Op::ONEOF_END: {
            vm.oneofStack.pop_back();
            vm.object.oneofExit();
            vm.codec.oneofExit();
        } break;
        case Op::ONEOF_ARM_BEGIN: {
            vm.object.oneofEnterArm(vm.oneofStack.back().oneofId,
                                    (uint32_t)instr.imm);
        } break;
        case Op::ONEOF_ARM_END: {
            vm.object.oneofExitArm();
        } break;
        case Op::ARRAY_BEGIN: {
            vm.arrayStack.emplace_back(ArrayFrame{
                .len = 0,
                .idx = uint32_t(-1),
            });
            vm.codec.arrayBegin();
        } break;
        case Op::ARRAY_END: {
            vm.arrayStack.pop_back();
            vm.codec.arrayEnd();
        } break;
        case Op::ARRAY_ELEM_BEGIN: {
            auto cidx = vm.arrayStack.back().idx;
            vm.object.arrayEnterElem(cidx);
        } break;
        case Op::ARRAY_ELEM_END: {
            vm.object.arrayExitElem();
        } break;
        case Op::ARRAY_NEXT: {
            vm.arrayStack.back().idx += 1;
            vm.flag = vm.arrayStack.back().idx < vm.arrayStack.back().len;
        } break;
        case Op::ENVELOPE_BEGIN:
            // TODO add this to main
            // TODO, we need a way to start the VM with envelope
            break;
        case Op::ENVELOPE_END:
            // TODO add this to main
            // TODO, we need a way to start the VM with envelope
            break;
        case Op::C_WRITE_SCALAR: {
            if constexpr (EncodeMode) {
                if (!writeScalar(instr, vm, vm.codec))
                    return false;
            } else {
                vm.error = VMError::InvalidInstr;
                return false;
            }
        } break;
        case Op::C_READ_SCALAR: {
            if constexpr (!EncodeMode) {
                if (!readScalar(instr, vm, vm.codec))
                    return false;
            } else {
                vm.error = VMError::InvalidInstr;
                return false;
            }
        } break;
        case Op::C_WRITE_OPT_PRESENT: {
            if constexpr (EncodeMode) {
                vm.codec.present(vm.flag == 1);
            } else {
                vm.error = VMError::InvalidInstr;
                return false;
            }
        } break;
        case Op::C_READ_OPT_PRESENT: {
            if constexpr (!EncodeMode) {
                vm.flag = vm.codec.present();
            } else {
                vm.error = VMError::InvalidInstr;
                return false;
            }
        } break;
        case Op::C_WRITE_ONEOF_ARM:
            if constexpr (EncodeMode) {
                vm.codec.oneofArm(instr.imm, vm.reg);
            }
            break;
        case Op::C_READ_ONEOF_ARM:
            if constexpr (!EncodeMode) {
                vm.reg =
                    vm.codec.oneofArm(vm.oneofStack.back().oneofId, instr.imm);
            }
            break;
        case Op::C_WRITE_ARRAY_LEN:
            if constexpr (EncodeMode) {
                vm.codec.arrayLen(instr.imm, vm.arrayStack.back().len);
            }
            break;
        case Op::C_READ_ARRAY_LEN:
            if constexpr (!EncodeMode) {
                vm.reg = vm.codec.arrayLen(instr.imm);
                vm.arrayStack.back().len = vm.reg;
            }
            break;
        case Op::C_WRITE_FIELD_ID: {
            if constexpr (EncodeMode) {
                vm.codec.fieldId(instr.imm);
            }
        } break;
        case Op::C_MATCH_FIELD_ID: {
            if constexpr (!EncodeMode) {
                vm.flag = vm.codec.fieldId(instr.imm);
            }
        } break;
        case Op::C_SKIP_FIELD_ID: {
            if constexpr (!EncodeMode) {
                vm.flag = vm.codec.skipFieldId(instr.imm);
            }
        } break;
        case Op::O_WRITE_SCALAR: {
            if constexpr (!EncodeMode) {
                if (!writeScalar(instr, vm, vm.object))
                    return false;
            }
        } break;
        case Op::O_READ_SCALAR: {
            if constexpr (EncodeMode) {
                if (!readScalar(instr, vm, vm.object))
                    return false;
            }
        } break;
        case Op::O_WRITE_OPT_PRESENT: {
            if constexpr (!EncodeMode) {
                vm.object.optSetPresent(vm.flag == 1);
            } else {
                vm.error = VMError::InvalidInstr;
                return false;
            }
        } break;
        case Op::O_READ_OPT_PRESENT: {
            if constexpr (EncodeMode) {
                vm.flag = vm.object.optPresent();
            }
        } break;
        case Op::O_WRITE_ONEOF_ARM: {
            if constexpr (!EncodeMode) {
                vm.object.oneofIndex(vm.oneofStack.back().oneofId, vm.reg);
            }
        } break;
        case Op::O_READ_ONEOF_ARM: {
            if constexpr (EncodeMode) {
                vm.reg = vm.object.oneofIndex(vm.oneofStack.back().oneofId, instr.imm);
            }
        } break;
        case Op::O_WRITE_ARRAY_LEN: {
            if constexpr (!EncodeMode) {
                vm.object.arrayPrepare(static_cast<uint32_t>(vm.reg));
                vm.arrayStack.back().len = static_cast<uint32_t>(vm.reg);
            } else {
                vm.error = VMError::InvalidInstr;
                return false;
            }
        } break;
        case Op::O_READ_ARRAY_LEN: {
            if constexpr (EncodeMode) {
                vm.reg = vm.object.arrayLen();
                vm.arrayStack.back().len = vm.reg;
            }
        } break;
        default:
            vm.error = VMError::InvalidInstr;
            return false;
    }

    if (!vm.object.ok()) {
        vm.error = VMError::ObjectError;
        return false;
    }
    if (!vm.codec.ok()) {
        vm.error = VMError::CodecError;
        return false;
    }

    vm.pc = nextPc;
    return true;
}

template <bool EncodeMode, class VM>
bool runVM(VM& vm, uint64_t typeId) {
    size_t stepCount = 0;
    reset(vm);

    if (vm.prog == nullptr) {
        vm.error = VMError::InvalidProgram;
        return false;
    }

    vm.reg = typeId;
    while (runInstr<EncodeMode>(vm)) {
    }

    // Exit successfully if there are no errors
    return vm.error == VMError::Ok;
}
}  // namespace detail

template <class ObjectAdapter, class CodecAdapter>
bool encode(VM<ObjectAdapter, CodecAdapter>& vm, uint64_t typeId) {
    return detail::runVM<true>(vm, typeId);
}
template <class ObjectAdapter, class CodecAdapter>
bool decode(VM<ObjectAdapter, CodecAdapter>& vm, uint64_t typeId) {
    return detail::runVM<false>(vm, typeId);
}

}  // namespace ao::schema::vm
