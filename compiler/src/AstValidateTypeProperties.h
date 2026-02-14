#pragma once

#include "ao/schema/AstBaseType.h"
#include "ao/schema/SemanticContext.h"
#include "ao/schema/Error.h"

namespace ao::schema {
void validateAstTypeProperties(
    ErrorContext& err,
    std::unordered_map<std::string, SemanticContext::Module>& modules);
}