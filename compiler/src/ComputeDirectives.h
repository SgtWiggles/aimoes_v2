#pragma once

#include <unordered_map>

#include "ao/schema/SemanticContext.h"


bool computeDirectives(
    ao::schema::ErrorContext& errors,
    std::unordered_map<std::string, ao::schema::SemanticContext::Module>&
        modules);
