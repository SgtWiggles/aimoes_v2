#pragma once

#include <string>

#include "ao/schema/VM.h"

namespace ao::schema::vm {

/// Produce a human readable, multi-line disassembly of a VM Program.
/// The output shows per-word addresses and decodes multi-word encodings
/// (EXT32, DISPATCH with following offsets, etc).
std::string prettyPrint(Program const& prog);

}  // namespace ao::schema::vm