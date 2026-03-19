#pragma once

#include "Error.h"
#include "IR.h"

#include <filesystem>
#include <iostream>

namespace ao::schema::cpp {
struct OutputFiles {
    std::ostream& header;
    std::ostream& ir;

    std::ostream* irHeader = nullptr;
};
bool generateCppCode(ir::IR const& ir, ErrorContext& errs, OutputFiles& files);

}  // namespace ao::schema::cpp
