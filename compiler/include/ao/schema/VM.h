#pragma once
#include <cstdint>
#include <vector>

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
    JMP32 = 0,          // imm32: rel32
    CALL32 = 1,         // imm32: rel32
    MSG_BEGIN32 = 2,    // imm32: msgId
    FIELD_BEGIN32 = 3,  // imm32: fieldId
    CALL_TYPE32 = 4,    // imm32: typeEntryId
    JT32 = 5,           // imm32: jtId
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

    // ============================================================
    // Common opcodes (shared by DecodeProgram + EncodeProgram)
    // Range: 0x00 - 0x1F
    // ============================================================

    HALT = 0x00,
    // Stop execution.

    JMP = 0x01,
    // imm16: rel16
    // pc += rel16 (relative in instruction words)

    JZ = 0x02,
    // imm16: rel16
    // if (flag == 0) pc += rel16 else pc++

    CALL = 0x03,
    // imm16: rel16
    // push return pc; pc += rel16

    RET = 0x04,
    // pop return pc

    EXT32 = 0x05,
    // a: ExtKind
    // Next word is imm32 payload interpreted according to a.

    MSG_BEGIN = 0x06,
    // imm16: msgId
    // adapter.msg_begin(msgId)

    MSG_END = 0x07,
    // adapter.msg_end()

    FIELD_BEGIN = 0x08,
    // imm16: fieldId
    // adapter.field_begin(fieldId)

    FIELD_END = 0x09,
    // adapter.field_end()

    FIELD_PRESENT = 0x0A,
    // flag = adapter.field_present()

    OPT_BEGIN = 0x0B,
    // a: BeginFlags
    // adapter.opt_begin(a)
    // push optional frame

    OPT_PRESENT = 0x0C,
    // flag = adapter.opt_present()

    OPT_END = 0x0D,
    // adapter.opt_end()
    // pop optional frame

    ARR_BEGIN = 0x0E,
    // a: BeginFlags
    // adapter.arr_begin(a)
    // push array frame (len set by ARR_LEN)

    ARR_LEN = 0x0F,
    // arr.len = adapter.arr_len()
    // arr.idx = 0

    ARR_NEXT = 0x10,
    // arr.idx++
    // flag = (arr.idx < arr.len)

    ARR_END = 0x11,
    // adapter.arr_end()
    // pop array frame

    ONEOF_BEGIN = 0x12,
    // a: BeginFlags
    // adapter.oneof_begin(a)
    // push oneof frame

    ONEOF_DISPATCH = 0x13,
    // a: JumpTableKind
    // imm16: jtId
    // Jump based on vm.oneofArm using jump table jtId.

    ONEOF_END = 0x14,
    // adapter.oneof_end()
    // pop oneof frame

    CALL_TYPE = 0x15,
    // imm16: typeEntryId
    // Indirect call to type entrypoint pc.

    SET_FLAG = 0x16,
    // a: 0 or 1
    // flag = a (utility instruction)

    // 0x18 - 0x1F reserved

    // ============================================================
    // Decode-only opcodes
    // Range: 0x20 - 0x3F
    // Only emitted in DecodeProgram.
    // ============================================================

    SCALAR_READ = 0x20,
    // a: ScalarKind
    // adapter.scalar_read(kind)
    // Adapter writes directly to destination storage for current field.

    FIELD_SKIP = 0x21,
    // adapter.field_skip()
    // Skip unknown/unhandled field (disk/net decode).

    ARR_ELEM_ENTER_D = 0x22,
    // adapter.arr_enter_elem_decode(arr.idx)
    // Enter element context for decode.

    ARR_ELEM_EXIT_D = 0x23,
    // adapter.arr_exit_elem_decode()

    ONEOF_SELECT = 0x24,
    // vm.oneofArm = adapter.oneof_select()
    // Select arm from input representation.

    SUBMSG_BEGIN_D = 0x25,
    // adapter.submsg_begin_decode()

    SUBMSG_END_D = 0x26,
    // adapter.submsg_end_decode()

    // Optional decode superinstructions
    FIELD_SCALAR_READ = 0x28,
    // a: ScalarKind
    // imm16: fieldId
    // Fused:
    //   field_begin(fieldId)
    //   if field_present():
    //     scalar_read(kind)
    //   field_end()

    ARR_ELEM_CALLTYPE_READ = 0x2A,
    // imm16: typeEntryId
    // Fused:
    //   arr_enter_elem_decode(idx)
    //   CALL_TYPE(typeEntryId)
    //   arr_exit_elem_decode()

    // ============================================================
    // Encode-only opcodes
    // Range: 0x40 - 0x5F
    // Only emitted in EncodeProgram.
    // ============================================================

    FIELD_WRITE_TAG = 0x40,
    // a: TagKind
    // adapter.field_write_tag(kind)
    // Disk/net: write tag/descriptor.
    // JSON/Lua: establish key context or no-op.

    SCALAR_GET = 0x41,
    // a: ScalarKind
    // adapter.scalar_get(kind, vm.scalarRegU64)

    SCALAR_WRITE = 0x42,
    // a: ScalarKind
    // adapter.scalar_write(kind, vm.scalarRegU64)

    OPT_VALUE = 0x43,
    // adapter.opt_enter_value()
    // Enter inner optional value context (encode).

    ARR_WRITE_BEGIN = 0x44,
    // a: TagKind (pack kind)
    // adapter.arr_write_begin(a)

    ARR_WRITE_END = 0x45,
    // adapter.arr_write_end(a)

    ARR_ELEM_ENTER_E = 0x46,
    // adapter.arr_enter_elem_encode(arr.idx)

    ARR_ELEM_EXIT_E = 0x47,
    // adapter.arr_exit_elem_encode()

    ONEOF_INDEX = 0x48,
    // vm.oneofArm = adapter.oneof_index()
    // Select arm from source representation.

    ONEOF_WRITE_TAG = 0x49,
    // a: TagKind
    // adapter.oneof_write_tag(a)

    ONEOF_ARM_VALUE = 0x4A,
    // adapter.oneof_enter_arm_value(vm.oneofArm)

    SUBMSG_BEGIN_E = 0x4B,
    // adapter.submsg_begin_encode()

    SUBMSG_END_E = 0x4C,
    // adapter.submsg_end_encode()

    // Optional encode superinstructions
    FIELD_SCALAR_WRITE = 0x50,
    // a: ScalarKind
    // imm16: fieldId
    // Fused:
    //   field_begin(fieldId)
    //   if field_present():
    //     field_write_tag(Default)
    //     scalar_get(kind)
    //     scalar_write(kind)
    //   field_end()

    ARR_ELEM_CALLTYPE_WRITE = 0x52,
    // imm16: typeEntryId
    // Fused:
    //   arr_enter_elem_encode(idx)
    //   CALL_TYPE(typeEntryId)
    //   arr_exit_elem_encode()
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

template <class Mode, class Adapter>
struct VM {
    Program const* prog = nullptr;
    Adapter adapter;

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
