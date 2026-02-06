#pragma once

#include <unordered_map>

#include "ao/schema/SemanticContext.h"

void computeNormalizedTypeParameters(
    ao::schema::ErrorContext& errs,
    std::unordered_map<std::string, ao::schema::SemanticContext::Module>&
        module);