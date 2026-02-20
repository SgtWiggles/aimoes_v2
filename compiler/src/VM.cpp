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

void generateVMDiskDecode(Assembler& assembler,
                          ao::schema::ir::IR const& irCode,
                          IdFor<ir::Message> msgId) {
    auto const& msg = irCode.messages[msgId.idx];
    assembler.emitMsgBegin(msgId, {});
    auto startLabel = assembler.useLabel();
    auto endLabel = assembler.useLabel();

    assembler.emit({Op::FIELD_GET_CURRENT_TAG, 0, 0}, {startLabel});
    assembler.emitFixup({Op::JZ, 0, 0}, endLabel, {});

    for (auto& f : msg.fields) {
        assembler.emitFieldBegin(f, {});
        auto fieldInfo = irCode.fields[f.idx];
        assembler.emitTypeCall(fieldInfo.type, {});
        assembler.emit({Op::FIELD_END, 0, 0}, {});
    }

    assembler.emit({Op::JMP, 0, 0}, startLabel);
    assembler.emit({Op::MSG_END, 0, 0}, {endLabel});
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

        std::visit(
            Overloaded{
                [&](ir::Scalar const& scalar) {
                    auto scalarOp =
                        encodeMode ? Op::SCALAR_WRITE : Op::SCALAR_READ;
                    switch (scalar.kind) {
                        case ir::Scalar::BOOL:
                            assembler.emit(Instr{scalarOp, ir::Scalar::BOOL, 1},
                                           {});
                            break;
                        case ir::Scalar::INT:
                            assembler.emit(
                                Instr{
                                    scalarOp,
                                    ir::Scalar::INT,
                                    static_cast<uint16_t>(scalar.width),
                                },
                                {});
                            break;
                        case ir::Scalar::UINT:
                            assembler.emit(
                                Instr{
                                    scalarOp,
                                    ir::Scalar::UINT,
                                    static_cast<uint16_t>(scalar.width),
                                },
                                {});
                            break;
                        case ir::Scalar::F32:
                            assembler.emit(
                                Instr{
                                    scalarOp,
                                    ir::Scalar::F32,
                                    32,
                                },
                                {});
                            break;
                        case ir::Scalar::F64:
                            assembler.emit(
                                Instr{
                                    scalarOp,
                                    ir::Scalar::F64,
                                    64,
                                },
                                {});
                            break;
                    }
                },
                [&](ir::Array const& arr) {
                    auto startLoop = assembler.useLabel();
                    auto endLoop = assembler.useLabel();
                    assembler.emit(Instr{Op::ARR_BEGIN, 0, 0}, {});
                    assembler.emit(Instr{Op::ARR_LEN, 0, 0}, {});
                    assembler.emit(Instr{Op::ARR_WRITE_BEGIN, 0, 0}, {});

                    assembler.emit(Instr{Op::ARR_ELEM_ENTER_E, 0, 0},
                                   {startLoop});

                    assembler.emitTypeCall(arr.type, {});

                    assembler.emit(Instr{Op::ARR_ELEM_EXIT_E, 0, 0}, {});
                    assembler.emit(Instr{Op::ARR_NEXT, 0, 0}, {});
                    assembler.emitFixup(Instr{Op::JZ, 0, 0}, endLoop, {});
                    assembler.emitFixup(Instr{Op::JMP, 0, 0}, startLoop, {});

                    assembler.emit(Instr{Op::ARR_WRITE_END, 0, 0}, {});
                    assembler.emit(Instr{Op::ARR_END, 0, 0}, {endLoop});
                },
                [&](ir::Optional const& opt) {
                    auto end = assembler.useLabel();
                    assembler.emit({Op::OPT_BEGIN, 0, 0}, {});
                    if (encodeMode) {
                        assembler.emit({Op::OPT_PRESENT, 0, 0}, {});
                    } else {
                        assembler.emit({Op::OPT_VALUE, 0, 0}, {});
                    }
                    assembler.emitFixup({Op::JZ, 0, 0}, end, {});
                    assembler.emitTypeCall(opt.type, {});
                    assembler.emit({Op::OPT_END, 0, 0}, {end});
                },
                [&](IdFor<ir::OneOf> oneof) {
                    // TODO clean this up and test, there's a lot of code which
                    // doesn't really make sense here
                    // This instruction set seems pretty verbose and we can
                    // probably clean this up Remember 1 VM for everything is
                    // the end goal, instructions shouldn't have different
                    // semantics based on encode/decode mode
                    // If an instruction isn't there then it should just error

                    std::vector<uint64_t> armLabels;
                    auto const& oneOfDesc = irCode.oneOfs[oneof.idx];
                    for (auto const& arm : oneOfDesc.arms) {
                        auto const& field = irCode.fields[arm.idx];
                        auto label = assembler.useLabel();
                        armLabels.push_back(label);
                    }
                    auto endLabel = assembler.useLabel();

                    // Start current oneof
                    assembler.emit({Op::ONEOF_BEGIN, 0, 0}, {});

                    // Write current tag
                    if (encodeMode) {
                        assembler.emit({Op::ONEOF_INDEX, 0, 0}, {});
                        assembler.emit({Op::ONEOF_WRITE_TAG, 0, 0}, {});
                        assembler.emit({Op::ONEOF_ARM_VALUE_ENTER_E, 0, 0}, {});
                    } else {
                        assembler.emit({Op::ONEOF_SELECT, 0, 0}, {});
                        assembler.emit({Op::ONEOF_ARM_VALUE_ENTER_D, 0, 0}, {});
                    }

                    assembler.emitDispatch(Op::ONEOF_DISPATCH, armLabels,
                                           endLabel, {});
                    for (size_t idx = 0; idx < oneOfDesc.arms.size(); ++idx) {
                        auto const& arm = oneOfDesc.arms[idx];
                        auto const& field = irCode.fields[arm.idx];
                        auto label = armLabels[idx];
                        assembler.emitTypeCall(field.type, label);
                        assembler.emitFixup({Op::JMP, 0, 0}, endLabel, {});
                    }

                    if (encodeMode) {
                        assembler.emit({Op::ONEOF_ARM_VALUE_EXIT_E, 0, 0}, {});
                    } else {
                        assembler.emit({Op::ONEOF_ARM_VALUE_EXIT_D, 0, 0}, {});
                    }
                    assembler.emit({Op::ONEOF_END, 0, 0}, {endLabel});
                },
                [&](IdFor<ir::Message> msgId) {
                    if (encodeMode || isNetMode) {
                        auto const& msg = irCode.messages[msgId.idx];
                        assembler.emitMsgBegin(msgId, {});
                        for (auto& f : msg.fields) {
                            assembler.emitFieldBegin(f, {});
                            auto fieldInfo = irCode.fields[f.idx];
                            assembler.emitTypeCall(fieldInfo.type, {});
                            assembler.emit({Op::FIELD_END, 0, 0}, {});
                        }
                        assembler.emit({Op::MSG_END, 0, 0}, {});
                    } else {
                        // Disk mode decode
                        generateVMDiskDecode(assembler, irCode, msgId);
                    }
                },
            },
            type.payload);

        assembler.emit(Instr{Op::RET, 0, 0}, {});
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
