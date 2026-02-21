#include "ao/schema/VM.h"

#include "ao/utils/Overloaded.h"

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
    uint64_t label;
};
struct FixUpDispatch {
    Op op;
    std::vector<uint64_t> label;
    uint64_t fail;
};

struct Entry {
    std::variant<Instr, FixUpInstr, FixUpDispatch> instr;
    std::optional<uint64_t> label;  // label defined at this location
};

struct Assembler {
    void emit(Instr instr, std::optional<uint64_t> label) {
        instructions.push_back(Entry{
            .instr = instr,
            .label = label,
        });
    }
    void emitFixup(Instr instr, uint64_t dest, std::optional<uint64_t> label) {
        instructions.push_back(Entry{
            .instr =
                FixUpInstr{
                    .instr = instr,
                    .label = dest,
                },
            .label = label,
        });
    }

    void emitDispatch(Op op,
                      std::vector<uint64_t> dispatchLabels,
                      uint64_t failLabel,
                      std::optional<uint64_t> label) {
        instructions.push_back(Entry{
            .instr =
                FixUpDispatch{
                    .op = op,
                    .label = std::move(dispatchLabels),
                    .fail = failLabel,
                },
            .label = label,
        });
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

    void emitExt32(Op baseOp,
                   ExtKind ext,
                   uint64_t idx,
                   std::optional<uint64_t> label) {
        if (idx <= std::numeric_limits<uint16_t>::max()) {
            emit(Instr{baseOp, 0, static_cast<uint16_t>(idx)}, {});
        } else if (idx <= std::numeric_limits<uint32_t>::max()) {
            emit(
                Instr{
                    Op::EXT32,
                    (uint8_t)ext,
                    0,
                },
                {});
            emit(decodeInstr(static_cast<uint32_t>(idx)), {});
        } else {
            // TODO error out, complain type space was too large.
            // In reality this should never happen
        }
    }

    uint64_t useLabel() {
        auto idx = labels.size();
        labels.push_back({
            .generated = false,
            .loc = 0,
        });
        return idx;
    }

    std::vector<Entry> instructions;
    std::vector<Label> labels;
};

struct VMGenerateContext {
    ErrorContext& errs;
    Program prog = {};  // For other assets

    std::vector<uint64_t> messageToTypeId;

    // TODO share string tables and stuff
    std::vector<Assembler> typePrograms;
};

// These generate the decode/encode functions.
// We still need to define entry points for each type that the program can jump
// into
void generateTypeProgram(VMGenerateContext& ctx,
                         ir::Type const& type,
                         Assembler& assembler,
                         ao::schema::ir::IR const& irCode,
                         bool const encodeMode,
                         bool const isNetMode) {
    std::visit(
        Overloaded{
            [&](ir::Scalar const& scalar) {
                if (encodeMode) {
                    assembler.emit(
                        {Op::A_READ_SCALAR, static_cast<uint8_t>(scalar.kind),
                         static_cast<uint16_t>(scalar.width)},
                        {});
                    assembler.emit(
                        {Op::C_WRITE_SCALAR, static_cast<uint8_t>(scalar.kind),
                         static_cast<uint16_t>(scalar.width)},
                        {});
                } else {
                    assembler.emit(
                        {Op::C_READ_SCALAR, static_cast<uint8_t>(scalar.kind),
                         static_cast<uint16_t>(scalar.width)},
                        {});
                    assembler.emit(
                        {Op::A_WRITE_SCALAR, static_cast<uint8_t>(scalar.kind),
                         static_cast<uint16_t>(scalar.width)},
                        {});
                }

                switch (scalar.kind) {
                    case ir::Scalar::BOOL:
                        ctx.errs.require(scalar.width == 1,
                                         {
                                             .code = ErrorCode::INTERNAL,
                                             .message = std::format(
                                                 "Unknown scalar type: {}",
                                                 (int)scalar.kind),
                                             .loc = {},
                                         });
                        break;
                    case ir::Scalar::INT:
                        break;
                    case ir::Scalar::UINT:
                        break;
                    case ir::Scalar::F32:
                        ctx.errs.require(scalar.width == 32,
                                         {
                                             .code = ErrorCode::INTERNAL,
                                             .message = std::format(
                                                 "Unknown scalar type: {}",
                                                 (int)scalar.kind),
                                             .loc = {},
                                         });
                        break;
                    case ir::Scalar::F64:
                        ctx.errs.require(scalar.width == 64,
                                         {
                                             .code = ErrorCode::INTERNAL,
                                             .message = std::format(
                                                 "Unknown scalar type: {}",
                                                 (int)scalar.kind),
                                             .loc = {},
                                         });
                        break;
                    default:
                        ctx.errs.fail({
                            .code = ErrorCode::INTERNAL,
                            .message = std::format("Unknown scalar type: {}",
                                                   (int)scalar.kind),
                            .loc = {},
                        });
                        break;
                }
            },
            [&](ir::Array const& arr) {
                assembler.emit({Op::ARRAY_BEGIN, 0, 0}, {});
                assembler.emit(
                    {encodeMode ? Op::A_READ_ARRAY_LEN : Op::C_READ_ARRAY_LEN,
                     0, 0},
                    {});
                assembler.emit(
                    {encodeMode ? Op::C_WRITE_ARRAY_LEN : Op::A_WRITE_ARRAY_LEN,
                     0, 0},
                    {});
                auto loopStart = assembler.useLabel();
                auto loopEnd = assembler.useLabel();
                assembler.emit({Op::ARRAY_NEXT, 0, 0}, {loopStart});
                assembler.emitFixup({Op::JZ, 0, 0}, loopEnd, {});
                assembler.emit({Op::ARRAY_ELEM_BEGIN, 0, 0}, {});
                assembler.emitTypeCall(arr.type, {});
                assembler.emit({Op::ARRAY_ELEM_END, 0, 0}, {});
                assembler.emitFixup({Op::JMP, 0, 0}, loopStart, {});
                assembler.emit({Op::ARRAY_END, 0, 0}, {loopEnd});
            },
            [&](ir::Optional const& opt) {
                assembler.emit({Op::OPT_BEGIN, 0, 0}, {});
                assembler.emit({encodeMode ? Op::A_READ_OPT_PRESENT
                                           : Op::C_READ_OPT_PRESENT,
                                0, 0},
                               {});
                assembler.emit({encodeMode ? Op::C_WRITE_OPT_PRESENT
                                           : Op::A_WRITE_OPT_PRESENT,
                                0, 0},
                               {});
                auto optEnd = assembler.useLabel();
                assembler.emitFixup({Op::JZ, 0, 0}, optEnd, {});

                assembler.emit({Op::OPT_BEGIN_VALUE, 0, 0}, {});
                assembler.emitTypeCall(opt.type, {});
                assembler.emit({Op::OPT_END_VALUE, 0, 0}, {});

                assembler.emit({Op::OPT_END, 0, 0}, {optEnd});
            },
            [&](IdFor<ir::OneOf> oneof) {
                assembler.emit({Op::ONEOF_BEGIN, 0, 0}, {});
                assembler.emit(
                    {encodeMode ? Op::A_READ_ONEOF_ARM : Op::A_READ_ONEOF_ARM,
                     0, 0},
                    {});
                assembler.emit(
                    {encodeMode ? Op::C_WRITE_ONEOF_ARM : Op::A_WRITE_ONEOF_ARM,
                     0, 0},
                    {});

                // TODO fix the case where we have more than 2^16 arms
                auto const& desc = irCode.oneOfs[oneof.idx];
                std::vector<uint64_t> labels;
                uint64_t failLabel;
                for (size_t idx = 0; idx < desc.arms.size(); ++idx) {
                    labels.push_back(assembler.useLabel());
                }

                assembler.emitDispatch(Op::DISPATCH, labels, failLabel, {});
                for (size_t idx = 0; idx < desc.arms.size(); ++idx) {
                    auto const& arm = desc.arms[idx];
                    auto const& fieldDesc = irCode.fields[arm.idx];
                    assembler.emit({Op::ONEOF_ARM_BEGIN, 0, 0}, {labels[idx]});
                    assembler.emitTypeCall(fieldDesc.type, {});
                    assembler.emit({Op::ONEOF_ARM_END, 0, 0}, {});
                }
                assembler.emit({Op::ONEOF_END, 0, 0}, {failLabel});
            },
            [&](IdFor<ir::Message> msgId) {
                auto const& desc = irCode.messages[msgId.idx];

                assembler.emit({Op::MSG_BEGIN, 0, 0}, {});
                for (auto fieldId : desc.fields) {
                    auto const& fieldDesc = irCode.fields[fieldId.idx];
                    auto endLabel = assembler.useLabel();
                    assembler.emit({Op::FIELD_BEGIN, 0, 0}, {});
                    if (encodeMode) {
                        if (!isNetMode) {
                            assembler.emit({Op::D_WRITE_FIELD_ID, 0,
                                            (uint16_t)fieldId.idx},
                                           {});
                        }
                        assembler.emitTypeCall(fieldDesc.type, {});
                    } else if (isNetMode) {  // net mode decode
                        assembler.emitTypeCall(fieldDesc.type, {});
                    } else {  // disk mode decode
                        auto skipLabel = assembler.useLabel();
                        assembler.emit(
                            {Op::D_MATCH_FIELD_ID, 0, (uint16_t)fieldId.idx},
                            {});
                        assembler.emitFixup({Op::JZ, 0, 0}, skipLabel, {});
                        assembler.emitTypeCall(fieldDesc.type, {});
                        assembler.emitFixup({Op::JMP, 0, 0}, endLabel, {});
                        assembler.emit(
                            {Op::D_SKIP_FIELD_ID, 0, (uint16_t)fieldId.idx},
                            {});
                    }

                    assembler.emit({Op::FIELD_END, 0, 0}, endLabel);
                }
                assembler.emit({Op::MSG_END, 0, 0}, {});
            },
        },
        type.payload);

    assembler.emit(Instr{Op::RET, 0, 0}, {});
}

void generateVMTypeCodes(VMGenerateContext& ctx,
                         ao::schema::ir::IR const& irCode,
                         bool const encodeMode,
                         bool const isNetMode) {
    // Generate unique labels for each message type to start
    ctx.messageToTypeId.resize(irCode.messages.size());

    for (size_t i = 0; i < irCode.types.size(); ++i) {
        auto const& type = irCode.types[i];
        auto& assembler = ctx.typePrograms.emplace_back();
        generateTypeProgram(ctx, type, assembler, irCode, encodeMode,
                            isNetMode);
    }
}

// Encode goes from Object -> Net Format
Program generateNetEncode(ao::schema::ir::IR const& irCode,
                          ErrorContext& errs) {
    VMGenerateContext ctx{errs};
    generateVMTypeCodes(ctx, irCode, true, true);
    return ctx.prog;
}

Program generateNetDecode(ao::schema::ir::IR const& irCode,
                          ErrorContext& errs) {
    VMGenerateContext ctx{errs};
    generateVMTypeCodes(ctx, irCode, false, true);
    return ctx.prog;
}
}  // namespace ao::schema::vm
