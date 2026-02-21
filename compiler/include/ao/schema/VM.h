#pragma once
#include <cstdint>
#include <vector>

#include "ao/schema/Codec.h"
#include "ao/schema/IR.h"

namespace ao::schema::vm {

/*
  Instruction encoding:
    32-bit word:
      u8  opcode
      u8  a
      u16 imm16

  EXT32 prefix:
    EXT32(extKind)
    <next 32-bit word is imm32 payload>

  Key modification vs previous version:
  - No C++ offsets in operands.
  - No JSON/Lua key ids in operands.
  - No tag offsets for oneof.
  - Bytecode references only logical ids (msgId, fieldId, typeEntryId, jtId).
  - All physical layout and naming live inside the generated adapter.
*/

// ------------------------------------------------------------
// Small enums
// ------------------------------------------------------------
using ScalarKind = ao::schema::ir::Scalar::ScalarKind;

enum class ExtKind : uint8_t {
    JMP32,          // imm32: rel32
    CALL32,         // imm32: rel32
    MSG_BEGIN32,    // imm32: msgId
    FIELD_BEGIN32,  // imm32: fieldId
    CALL_TYPE32,    // imm32: typeEntryId
    DISPATCH32,     // imm32: dispatch
    JT32,           // imm32: jtId
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

    DISPATCH,
    // a: register
    // imm16: branch count
    // 32 bit rel jumps for each dispatch

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


    // Codec functions

    // Top level message framing
    // Disk format these are standard TLV using message numbers
    // Net format just the message id
    C_FRAME_BEGIN,
    C_FRAME_END,

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
    D_WRITE_FIELD_ID,
    // adapter.writeFieldNumber(reg)
    D_MATCH_FIELD_ID,
    // imm16: expected field id
    // flag = expected == current field id

    D_SKIP_FIELD_ID,
    // imm16: original expected field id
    // flag = field was skipped
    // codec.skip_field

    // Adapter Functions
    A_WRITE_SCALAR,
    // Switch on scalar type and write it
    A_READ_SCALAR,
    // reg = Read bits from scalar type

    A_WRITE_OPT_PRESENT,
    // adapter.write_opt_present(flag)
    A_READ_OPT_PRESENT,
    // flag = opt is present

    A_WRITE_ONEOF_ARM,
    // adapter.oneofEnterArm(reg)
    A_READ_ONEOF_ARM,
    // reg = adapter.oneofChoose()

    A_WRITE_ARRAY_LEN,
    // adapter.arrLen(reg)
    A_READ_ARRAY_LEN,
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
struct Program {
    std::vector<uint32_t> codeWords;
    std::vector<uint32_t> typeEntryPcWords;
    std::vector<JumpTableMeta> jumpTables;
    std::vector<uint32_t> jumpTableDataWords;
};

Program generateNetEncode(ao::schema::ir::IR const& irCode, ErrorContext& errs);
Program generateNetDecode(ao::schema::ir::IR const& irCode, ErrorContext& errs);

template <class ObjectAdapter, class CodecAdapter>
struct VM {
    static constexpr bool IsBitCodec =
        std::is_same_v<typename CodecAdapter::ChunkSize, CodecBits>;
    Program const* prog = nullptr;
    ObjectAdapter object;
    CodecAdapter codec;

    uint32_t pc = 0;
    uint8_t flag = 0;
    int32_t oneofArm = -1;
    uint64_t scalarRegU64 = 0;

    uint8_t* dstBase = nullptr;
    uint8_t const* srcBase = nullptr;

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
        uint32_t _reserved = 0;
    };
    std::vector<CallFrame> m_callStack;
    std::vector<ArrayFrame> m_arrayStack;
    std::vector<OptionalFrame> m_optionalStack;
    std::vector<OneofFrame> m_oneofStack;
};
}  // namespace ao::schema::vm
