#include <ao/schema/Assembler.h>

#include <catch2/catch_all.hpp>

using namespace ao::schema::vm;

TEST_CASE("Assembler fits in", "[assembler]") {
    for (int64_t i = std::numeric_limits<int16_t>::min();
         i <= std::numeric_limits<int16_t>::max(); ++i) {
        INFO(i);
        bool res = fitsIn<int16_t>(i);
        REQUIRE(res);
    }
    REQUIRE(fitsIn<int16_t>((int64_t)11));
}

TEST_CASE("Assembler forward short jumps", "[assembler]") {
    Assembler assembler{};
    auto jumpLabel = assembler.useLabel();
    auto dispatchLabel1 = assembler.useLabel();
    auto dispatchLabel2 = assembler.useLabel();

    assembler.jmp(jumpLabel, {});
    assembler.jz(jumpLabel, {});
    assembler.emitDispatch({dispatchLabel1, dispatchLabel2}, jumpLabel, {});
    assembler.emit(Instr{Op::HALT, 0, 0}, {});
    assembler.emit(Instr{Op::HALT, 0, 0}, {});
    assembler.emit(Instr{Op::HALT, 0, 0}, {});
    assembler.emit(Instr{Op::HALT, 3, 0}, {dispatchLabel1});
    assembler.emit(Instr{Op::HALT, 2, 0}, {dispatchLabel2});
    assembler.emit(Instr{Op::HALT, 1, 0}, jumpLabel);

    ao::schema::ErrorContext ctx;
    auto code = assembler.assemble(ctx);
    INFO(ctx.toString());
    REQUIRE(ctx.ok());
    REQUIRE(code.size() > 6);
    REQUIRE(decodeInstr(code.at(0)).op == Op::JMP);
    REQUIRE(decodeInstr(code.at(0)).imm == code.size() - 1);
    REQUIRE(decodeInstr(code.at(1)).op == Op::JZ);
    REQUIRE(decodeInstr(code.at(1)).imm == code.size() - 2);

    auto dispatchPos = 2;
    auto dispatch = decodeInstr(code.at(dispatchPos));

    REQUIRE(dispatch == Instr{Op::DISPATCH, 0, 2});

    auto dispatch1Offset = std::bit_cast<int32_t>(code.at(dispatchPos + 1));
    auto dispatch2Offset = std::bit_cast<int32_t>(code.at(dispatchPos + 2));
    auto dispatchFailOffset = std::bit_cast<int32_t>(code.at(dispatchPos + 3));
    REQUIRE(decodeInstr(code.at(dispatchPos + dispatch1Offset)) ==
            Instr{Op::HALT, 3, 0});
    REQUIRE(decodeInstr(code.at(dispatchPos + dispatch2Offset)) ==
            Instr{Op::HALT, 2, 0});
    REQUIRE(decodeInstr(code.at(dispatchPos + dispatchFailOffset)) ==
            Instr{Op::HALT, 1, 0});

    REQUIRE(decodeInstr(code.back()).op == Op::HALT);
}

TEST_CASE("Assembler backward short jumps", "[assembler]") {
    Assembler assembler{};
    auto jumpLabel = assembler.useLabel();
    auto dispatchLabel1 = assembler.useLabel();
    auto dispatchLabel2 = assembler.useLabel();

    assembler.emit(Instr{Op::HALT, 1, 0}, jumpLabel);
    assembler.emit(Instr{Op::HALT, 2, 0}, {dispatchLabel1});
    assembler.emit(Instr{Op::HALT, 3, 0}, {dispatchLabel2});
    assembler.emit(Instr{Op::HALT, 0, 0}, {});
    assembler.emit(Instr{Op::HALT, 0, 0}, {});
    assembler.emit(Instr{Op::HALT, 0, 0}, {});
    assembler.emitDispatch({dispatchLabel1, dispatchLabel2}, jumpLabel, {});
    assembler.jz(jumpLabel, {});
    assembler.jmp(jumpLabel, {});

    ao::schema::ErrorContext ctx;
    auto code = assembler.assemble(ctx);
    INFO(ctx.toString());
    REQUIRE(ctx.ok());
    REQUIRE(code.size() > 6);
    REQUIRE(decodeInstr(code.at(code.size() - 1)).op == Op::JMP);
    REQUIRE(std::bit_cast<int16_t>(decodeInstr(code.at(code.size() - 1)).imm) ==
            -((int64_t)code.size() - 1));
    REQUIRE(decodeInstr(code.at(code.size() - 2)).op == Op::JZ);
    REQUIRE(std::bit_cast<int16_t>(decodeInstr(code.at(code.size() - 2)).imm) ==
            -((int64_t)code.size() - 2));

    auto dispatchPos = code.size() - 6;
    auto dispatch = decodeInstr(code.at(dispatchPos));
    REQUIRE(dispatch.op == Op::DISPATCH);
    REQUIRE(dispatch.imm == 2);

    auto dispatch1Offset = std::bit_cast<int32_t>(code.at(dispatchPos + 1));
    auto dispatch2Offset = std::bit_cast<int32_t>(code.at(dispatchPos + 2));
    auto dispatchFailOffset = std::bit_cast<int32_t>(code.at(dispatchPos + 3));
    REQUIRE(decodeInstr(code.at(dispatchPos + dispatch1Offset)) ==
            Instr{Op::HALT, 2, 0});
    REQUIRE(decodeInstr(code.at(dispatchPos + dispatch2Offset)) ==
            Instr{Op::HALT, 3, 0});
    REQUIRE(decodeInstr(code.at(dispatchPos + dispatchFailOffset)) ==
            Instr{Op::HALT, 1, 0});

    for (int i = 3; i < 6; ++i) {
        auto instr = decodeInstr(code.at(i));
        REQUIRE(instr.op == Op::HALT);
        REQUIRE(instr.imm == 0);
        REQUIRE(instr.mode == 0);
    }
}

TEST_CASE("Assembler long jumps", "[assembler]") {
    Assembler assembler{};
    auto jumpLabel1 = assembler.useLabel();
    auto jumpLabel2 = assembler.useLabel();

    assembler.jmp(jumpLabel1, jumpLabel2);
    for (size_t i = 0; i <= std::numeric_limits<int16_t>::max(); ++i) {
        assembler.emit(Instr{Op::HALT, 0, 0}, {});
    }
    assembler.jz(jumpLabel2, jumpLabel1);

    ao::schema::ErrorContext errs;
    auto code = assembler.assemble(errs);
    REQUIRE(errs.ok());
    REQUIRE(code.size() > std::numeric_limits<int16_t>::max());
    auto firstInstr = decodeInstr(code.at(0));
    REQUIRE(firstInstr.op == Op::EXT32);
    REQUIRE(firstInstr.mode == (uint8_t)ExtKind::JMP32);

    auto jump1 = std::bit_cast<int32_t>(code.at(1));

    auto secondInstr = decodeInstr(code.at(jump1));
    REQUIRE(secondInstr.op == Op::EXT32);
    REQUIRE(secondInstr.mode == (uint8_t)ExtKind::JZ32);

    auto jump2 = std::bit_cast<int32_t>(code.at(jump1 + 1));
    REQUIRE(jump1 == -jump2);
}
