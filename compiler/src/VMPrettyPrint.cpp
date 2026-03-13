#include "ao/schema/VMPrettyPrint.h"

#include <format>
#include <sstream>

namespace ao::schema::vm {

static std::string opName(Op op) {
    switch (op) {
#define CASE(x) \
    case Op::x: \
        return #x;
        CASE(HALT)
        CASE(JMP)
        CASE(JZ)
        CASE(CALL)
        CASE(RET)
        CASE(EXT32)
        CASE(CALL_TYPE)
        CASE(CALL_TYPE_INDIRECT)
        CASE(DISPATCH)
        CASE(MSG_BEGIN)
        CASE(MSG_END)
        CASE(FIELD_BEGIN)
        CASE(FIELD_END)
        CASE(OPT_BEGIN)
        CASE(OPT_END)
        CASE(OPT_BEGIN_VALUE)
        CASE(OPT_END_VALUE)
        CASE(ONEOF_BEGIN)
        CASE(ONEOF_END)
        CASE(ONEOF_ARM_BEGIN)
        CASE(ONEOF_ARM_END)
        CASE(ARRAY_BEGIN)
        CASE(ARRAY_END)
        CASE(ARRAY_ELEM_BEGIN)
        CASE(ARRAY_ELEM_END)
        CASE(ARRAY_NEXT)
        CASE(ENVELOPE_BEGIN)
        CASE(ENVELOPE_END)
        CASE(C_WRITE_FIELD_ID)
        CASE(C_MATCH_FIELD_ID)
        CASE(C_SKIP_FIELD)
        CASE(C_WRITE_SCALAR)
        CASE(C_READ_SCALAR)
        CASE(C_WRITE_OPT_PRESENT)
        CASE(C_READ_OPT_PRESENT)
        CASE(C_WRITE_ONEOF_ARM)
        CASE(C_READ_ONEOF_ARM)
        CASE(C_WRITE_ARRAY_LEN)
        CASE(C_READ_ARRAY_LEN)
        CASE(O_WRITE_SCALAR)
        CASE(O_READ_SCALAR)
        CASE(O_WRITE_OPT_PRESENT)
        CASE(O_READ_OPT_PRESENT)
        CASE(O_WRITE_ONEOF_ARM)
        CASE(O_READ_ONEOF_ARM)
        CASE(O_WRITE_ARRAY_LEN)
        CASE(O_READ_ARRAY_LEN)
#undef CASE
        default:
            return "UNKNOWN_OP";
    }
}

static std::string extKindName(ExtKind k) {
    switch (k) {
        case ExtKind::JMP32:
            return "JMP32";
        case ExtKind::JZ32:
            return "JZ32";
        case ExtKind::MSG_BEGIN32:
            return "MSG_BEGIN32";
        case ExtKind::FIELD_BEGIN32:
            return "FIELD_BEGIN32";
        case ExtKind::CALL_TYPE32:
            return "CALL_TYPE32";
        case ExtKind::DISPATCH32:
            return "DISPATCH32";
        default:
            return "UNKNOWN_EXT";
    }
}

std::string prettyPrint(Program const& prog) {
    std::ostringstream out;
    auto const& words = prog.codeWords;
    out << std::format("Program: {} words\n", words.size());

    for (size_t pc = 0; pc < words.size();) {
        uint32_t raw = words[pc];
        Instr instr = decodeInstr(raw);
        out << std::format("{:04} 0x{:08X}  {:16} ", pc, raw, opName(instr.op));

        // Common immediate interpretation helpers
        auto imm16_u = instr.imm;
        auto imm16_s = static_cast<int16_t>(instr.imm);

        switch (instr.op) {
            case Op::EXT32: {
                // EXT32 consumes the next 32-bit word as payload
                ExtKind ek = static_cast<ExtKind>(instr.mode);
                out << std::format("<{}> ", extKindName(ek));
                if (pc + 1 >= words.size()) {
                    out << "[MISSING EXT32 PAYLOAD]\n";
                    ++pc;
                    break;
                }
                uint32_t payload = words[pc + 1];
                // Print payload as unsigned, signed (where relevant) and hex
                out << std::format("payload=0x{:08X} ({})\n", payload, payload);
                pc += 2;
                // If this EXT32 encodes a DISPATCH (DISPATCH32) we should try
                // to display the following dispatch offsets; but the assembler
                // produces the offsets as raw int32 words immediately after the
                // EXT32/primary word. We can't know a precise count here except
                // when the payload itself encodes the branch count. Print that
                // hint as well.
                if (ek == ExtKind::DISPATCH32) {
                    // Treat payload as branch count (uint32_t)
                    uint32_t branchCount = payload;
                    out << std::format("      dispatch branch count hint: {}\n",
                                       branchCount);
                    // Print up to branchCount + 1 offsets if available
                    for (uint32_t i = 0; i <= branchCount && pc < words.size();
                         ++i) {
                        int32_t off = std::bit_cast<int32_t>(words[pc]);
                        out << std::format(
                            "      [{}] offset = {:+d} (pc -> {})\n", i, off,
                            (int64_t)pc + off);
                        ++pc;
                    }
                }
                break;
            }
            case Op::DISPATCH: {
                // imm16 = branch count
                uint32_t branchCount = imm16_u;
                out << std::format("branches={} \n", branchCount);
                // After the DISPATCH instruction there are branchCount + 1
                // 32-bit signed relative offsets (each is a word)
                size_t expected = (size_t)branchCount + 1;
                for (size_t i = 0; i < expected && pc + 1 + i < words.size();
                     ++i) {
                    uint32_t w = words[pc + 1 + i];
                    int32_t off = std::bit_cast<int32_t>(w);
                    out << std::format("      [{}] offset = {:+d} (pc -> {})\n",
                                       i, off, (int64_t)(pc + 1 + i) + off);
                }
                pc += 1 + expected;
                break;
            }
            case Op::JMP:
            case Op::JZ:
            case Op::CALL: {
                // imm16 is a signed relative offset (int16)
                out << std::format("imm16 = {:+d} -> target {}\n", imm16_s,
                                   (int64_t)pc + imm16_s);
                ++pc;
                break;
            }
            case Op::CALL_TYPE:
            case Op::CALL_TYPE_INDIRECT:
            case Op::MSG_BEGIN:
            case Op::FIELD_BEGIN:
            case Op::ONEOF_ARM_BEGIN:
            case Op::C_WRITE_FIELD_ID:
            case Op::C_MATCH_FIELD_ID:
            case Op::C_SKIP_FIELD:
            case Op::C_WRITE_SCALAR:
            case Op::C_READ_SCALAR:
            case Op::C_WRITE_ONEOF_ARM:
            case Op::C_READ_ONEOF_ARM:
            case Op::C_WRITE_ARRAY_LEN:
            case Op::C_READ_ARRAY_LEN:
            case Op::O_WRITE_SCALAR:
            case Op::O_READ_SCALAR:
            case Op::O_READ_ONEOF_ARM:
            case Op::ARRAY_NEXT:
                // These commonly use imm as an unsigned or bit-width immediate
                out << std::format("imm16 = {} \n", imm16_u);
                ++pc;
                break;
            default:
                // No extra words consumed
                out << "\n";
                ++pc;
                break;
        }
    }

    // Print entry tables
    out << std::format("\nType entry PCs: {} items\n", prog.typeEntryPc.size());
    for (size_t i = 0; i < prog.typeEntryPc.size(); ++i) {
        out << std::format("  type[{}] -> {}\n", i, prog.typeEntryPc[i]);
    }
    out << std::format("\nMessage entry PCs: {} items\n",
                       prog.msgEntryPc.size());
    for (size_t i = 0; i < prog.msgEntryPc.size(); ++i) {
        out << std::format("  msg[{}] -> {}\n", i, prog.msgEntryPc[i]);
    }

    return out.str();
}

}  // namespace ao::schema::vm
