#pragma once

#include "IR.h"
#include "Error.h"

#include <iostream>

namespace ao::schema::cpp {
bool generateCppCode(ir::IR const& ir, ErrorContext& errs, std::ostream& out);
}
