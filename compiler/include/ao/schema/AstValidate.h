#pragma once

#include "Ast.h"
#include "Error.h"

namespace ao::schema {
struct ValidateContext {
    std::vector<Error> errors;
};


}  // namespace ao::schema
