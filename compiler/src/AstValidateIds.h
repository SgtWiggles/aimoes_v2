#pragma once

#include <unordered_map>

#include "ao/schema/SemanticContext.h"

bool validateGlobalMessageIds(
    ao::schema::ErrorContext& errors,
    std::unordered_map<std::string, ao::schema::SemanticContext::Module>&
        modules);
bool validateFieldNumbers(
    ao::schema::ErrorContext& errors,
    std::unordered_map<std::string, ao::schema::SemanticContext::Module>&
        modules);
