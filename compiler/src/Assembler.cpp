#include "ao/schema/Assembler.h"

#include <format>

#include "ao/utils/Overloaded.h"

namespace ao::schema::vm {

void Assembler::emit(Instr instr, std::optional<uint64_t> label) {
    instructions.push_back(Entry{
        .instr = instr,
        .label = label,
    });
}

void Assembler::emitFixup(Instr instr,
                          ExtKind ext,
                          uint64_t dest,
                          std::optional<uint64_t> label) {
    instructions.push_back(Entry{
        .instr =
            FixUpInstr{
                .instr = instr,
                .ext = ext,
                .label = dest,
            },
        .label = label,
    });
}

void Assembler::emitDispatch(std::vector<uint64_t> dispatchLabels,
                             uint64_t failLabel,
                             std::optional<uint64_t> label) {
    emitExt32(Op::DISPATCH, ExtKind::DISPATCH32, dispatchLabels.size(), label);
    int64_t offset = 1;
    for (auto dest : dispatchLabels) {
        instructions.emplace_back(FixUp32{
            .label = dest,
            .offset = offset,
        });
        ++offset;
    }
    instructions.emplace_back(FixUp32{
        .label = failLabel,
        .offset = offset,
    });
}

void Assembler::emitExt32(Op baseOp,
                          ExtKind ext,
                          uint64_t idx,
                          std::optional<uint64_t> label) {
    if (idx <= std::numeric_limits<uint16_t>::max()) {
        emit(Instr{baseOp, 0, static_cast<uint16_t>(idx)}, label);
    } else if (idx <= std::numeric_limits<uint32_t>::max()) {
        emit(
            Instr{
                Op::EXT32,
                (uint8_t)ext,
                0,
            },
            label);
        emit(decodeInstr(static_cast<uint32_t>(idx)), {});
    } else {
        // TODO error out, complain type space was too large.
        // In reality this should never happen
    }
}
uint64_t Assembler::useLabel() {
    auto idx = labels.size();
    labels.push_back({
        .generated = false,
        .loc = 0,
    });
    return idx;
}

size_t getMaximumProgramSize(std::vector<Entry> const& entries) {
    size_t size = 0;
    for (auto const& e : entries) {
        std::visit(Overloaded{
                       [&size](Instr) { ++size; },
                       [&size](FixUpInstr) { size += 2; },
                       [&size](FixUp32) { ++size; },
                   },
                   e.instr);
    }
    return size;
}

std::vector<uint32_t> computeJumpLabels(Assembler const& assembler,
                                        ErrorContext& err) {
    size_t prevSize = 0;
    bool done = false;
    std::unordered_map<size_t, size_t> labels;
    std::vector<Entry> currentEntries = assembler.instructions;
    std::vector<Entry> nextEntries;

    auto maxSize = getMaximumProgramSize(assembler.instructions);
    nextEntries.reserve(maxSize);
    currentEntries.reserve(maxSize);

    while (!done) {
        done = true;
        bool errored = false;

        labels.clear();
        size_t entryIdx = 0;
        for (auto const& entry : currentEntries) {
            if (!entry.label) {
                ++entryIdx;
                continue;
            }
            if (labels.contains(*entry.label)) {
                err.fail({
                    .code = ErrorCode::INTERNAL,
                    .message = std::format(
                        "Assembler: Multiple declarations of label: {}",
                        *entry.label),
                    .loc = {},
                });
                errored = true;
            }
            labels[*entry.label] = entryIdx;
            ++entryIdx;
        }
        if (errored)
            return {};

        // Compute required jumps, widen small instructions
        nextEntries.clear();
        for (auto const& entry : currentEntries) {
            std::visit(
                Overloaded{
                    [&](Instr instr) { nextEntries.emplace_back(entry); },
                    [&](FixUpInstr instr) {
                        auto iter = labels.find(instr.label);
                        if (iter == labels.end()) {
                            err.fail({
                                .code = ErrorCode::INTERNAL,
                                .message = std::format(
                                    "Assembler: Use of undefined label: {}",
                                    instr.label),
                                .loc = {},
                            });
                            errored = true;
                            return;
                        }
                        auto dest = iter->second;
                        int64_t jumpDist = dest - (int64_t)nextEntries.size();

                        if (fitsIn<int16_t>(jumpDist)) {
                            nextEntries.emplace_back(entry);
                        } else if (fitsIn<int32_t>(jumpDist)) {
                            nextEntries.emplace_back(Entry{
                                .instr =
                                    Instr{
                                        Op::EXT32,
                                        (uint8_t)instr.ext,
                                        0,
                                    },
                                .label = entry.label,
                            });
                            nextEntries.emplace_back(Entry{
                                .instr =
                                    FixUp32{
                                        .label = instr.label,
                                        .offset = 1,
                                    },
                                .label = {},
                            });
                            done = false;
                        } else {
                            err.fail({
                                .code = ErrorCode::INTERNAL,
                                .message = std::format(
                                    "Assembler: failed to "
                                    "fixup, offset from {} to {} too far: {}",
                                    nextEntries.size(), dest, jumpDist),
                                .loc = {},
                            });
                            errored = true;
                        }
                    },
                    [&](FixUp32 instr) {
                        auto iter = labels.find(instr.label);
                        if (iter == labels.end()) {
                            err.fail({
                                .code = ErrorCode::INTERNAL,
                                .message = std::format(
                                    "Assembler: Use of undefined label: {}",
                                    instr.label),
                                .loc = {},
                            });
                            errored = true;
                        }
                        nextEntries.emplace_back(entry);
                    },
                },
                entry.instr);
        }

        if (errored)
            return {};
        std::swap(currentEntries, nextEntries);
    }

    // Everything has a stable location now
    // Patch in real locations
    std::vector<uint32_t> ops = {};
    for (auto& entry : currentEntries) {
        std::visit(Overloaded{
                       [&](Instr instr) { ops.push_back(instr.pack()); },
                       [&](FixUpInstr instr) {
                           auto labelPos = labels[instr.label];
                           int16_t offset = labelPos - ops.size();
                           instr.instr.imm = std::bit_cast<uint16_t>(offset);
                           ops.push_back(instr.instr.pack());
                       },
                       [&](FixUp32 instr) {
                           auto labelPos = labels[instr.label];
                           int32_t offset = labelPos - ops.size();
                           ops.push_back(std::bit_cast<uint32_t>(offset) +
                                         instr.offset);
                       },
                   },
                   entry.instr);
    }
    return ops;
}

std::vector<uint32_t> Assembler::assemble(ErrorContext& err) const {
    return computeJumpLabels(*this, err);
}

}  // namespace ao::schema::vm