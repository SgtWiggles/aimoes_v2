#include "ao/schema/VM.h"

#include "ao/schema/Assembler.h"
#include "ao/utils/Overloaded.h"

namespace ao::schema::vm {

struct VMGenerateContext {
    ErrorContext& errs;
    Program prog = {};  // For other assets

    std::vector<uint64_t> messageToTypeId;

    // TODO share string tables and stuff
    std::vector<Assembler> typePrograms;
    Assembler mainProgram;
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
                        {Op::O_READ_SCALAR, static_cast<uint8_t>(scalar.kind),
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
                        {Op::O_WRITE_SCALAR, static_cast<uint8_t>(scalar.kind),
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
                uint16_t lenbits = 0;
                if (arr.maxSize)
                    lenbits =
                        std::max(std::bit_width((uint64_t)*arr.maxSize), 1);
                assembler.emit({Op::ARRAY_BEGIN, 0, 0}, {});
                assembler.emit(
                    {encodeMode ? Op::O_READ_ARRAY_LEN : Op::C_READ_ARRAY_LEN,
                     0, lenbits},
                    {});
                assembler.emit(
                    {encodeMode ? Op::C_WRITE_ARRAY_LEN : Op::O_WRITE_ARRAY_LEN,
                     0, lenbits},
                    {});
                auto loopStart = assembler.useLabel();
                auto loopEnd = assembler.useLabel();
                assembler.emit({Op::ARRAY_NEXT, 0, 0}, {loopStart});
                assembler.jz(loopEnd, {});
                assembler.emit({Op::ARRAY_ELEM_BEGIN, 0, 0}, {});
                assembler.emitTypeCall(arr.type, {});
                assembler.emit({Op::ARRAY_ELEM_END, 0, 0}, {});
                assembler.jmp(loopStart, {});
                assembler.emit({Op::ARRAY_END, 0, 0}, {loopEnd});
            },
            [&](ir::Optional const& opt) {
                assembler.emit({Op::OPT_BEGIN, 0, 0}, {});
                assembler.emit({encodeMode ? Op::O_READ_OPT_PRESENT
                                           : Op::C_READ_OPT_PRESENT,
                                0, 0},
                               {});
                assembler.emit({encodeMode ? Op::C_WRITE_OPT_PRESENT
                                           : Op::O_WRITE_OPT_PRESENT,
                                0, 0},
                               {});
                auto optEnd = assembler.useLabel();
                assembler.jz(optEnd, {});

                assembler.emit({Op::OPT_BEGIN_VALUE, 0, 0}, {});
                assembler.emitTypeCall(opt.type, {});
                assembler.emit({Op::OPT_END_VALUE, 0, 0}, {});

                assembler.emit({Op::OPT_END, 0, 0}, {optEnd});
            },
            [&](IdFor<ir::OneOf> oneof) {
                auto const& desc = irCode.oneOfs[oneof.idx];
                uint16_t armBits =
                    std::min(std::max(std::bit_width(desc.arms.size()), 1), 64);
                assembler.emit({Op::ONEOF_BEGIN, 0, 0}, {});
                assembler.emit(
                    {encodeMode ? Op::O_READ_ONEOF_ARM : Op::C_READ_ONEOF_ARM,
                     0, armBits},
                    {});
                assembler.emit(
                    {encodeMode ? Op::C_WRITE_ONEOF_ARM : Op::O_WRITE_ONEOF_ARM,
                     0, armBits},
                    {});

                // TODO fix the case where we have more than 2^16 arms
                std::vector<uint64_t> labels;
                for (size_t idx = 0; idx < desc.arms.size(); ++idx) {
                    labels.push_back(assembler.useLabel());
                }

                uint64_t failLabel = assembler.useLabel();
                assembler.emitDispatch(labels, failLabel, {});
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
                        assembler.jz(skipLabel, {});
                        assembler.emitTypeCall(fieldDesc.type, {});
                        assembler.jmp(endLabel, {});
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
    for (size_t i = 0; i < irCode.types.size(); ++i) {
        auto const& type = irCode.types[i];
        auto& assembler = ctx.typePrograms.emplace_back();
        generateTypeProgram(ctx, type, assembler, irCode, encodeMode,
                            isNetMode);
    }
}

void generateVMMain(VMGenerateContext& ctx, ao::schema::ir::IR const& irCode) {
    auto& assembler = ctx.mainProgram;
    assembler.emit(Instr{Op::CALL_TYPE_INDIRECT, 0, 0}, {});
}

void linkTypeCodes(VMGenerateContext& ctx, ao::schema::ir::IR const& irCode) {
    ctx.errs.require(ctx.typePrograms.size() == irCode.types.size(),
                     {
                         .code = ErrorCode::INTERNAL,
                         .message = std::format(
                             "Mismatch between type programs and types size"),
                         .loc = {},
                     });

    auto linked = ctx.mainProgram.assemble(ctx.errs);

    std::vector<uint32_t> typePcOffsets;
    typePcOffsets.reserve(ctx.typePrograms.size());
    for (auto const& assembler : ctx.typePrograms) {
        typePcOffsets.emplace_back(linked.size());

        auto code = assembler.assemble(ctx.errs);
        linked.insert(linked.end(), code.begin(), code.end());
    }

    ctx.prog.codeWords = std::move(linked);
    ctx.prog.typeEntryPc = std::move(typePcOffsets);
    

    // Build mapping between messages and types
    ctx.prog.msgEntryPc.resize(irCode.messages.size());
    for (size_t typeIdx = 0; typeIdx < irCode.types.size(); ++typeIdx) {
        auto msg =
            std::get_if<IdFor<ir::Message>>(&irCode.types[typeIdx].payload);
        if (!msg)
            continue;
        ctx.prog.msgEntryPc[msg->idx] = ctx.prog.typeEntryPc[typeIdx];
    }
}

Program generateProgram(ao::schema::ir::IR const& irCode,
                        ErrorContext& errs,
                        bool encode,
                        bool net) {
    VMGenerateContext ctx{errs};
    generateVMMain(ctx, irCode);
    generateVMTypeCodes(ctx, irCode, encode, net);
    linkTypeCodes(ctx, irCode);
    return ctx.prog;
}

// Encode goes from Object -> Net Format
Program generateNetEncode(ao::schema::ir::IR const& irCode,
                          ErrorContext& errs) {
    return generateProgram(irCode, errs, true, true);
}

Program generateNetDecode(ao::schema::ir::IR const& irCode,
                          ErrorContext& errs) {
    return generateProgram(irCode, errs, false, true);
}
}  // namespace ao::schema::vm
