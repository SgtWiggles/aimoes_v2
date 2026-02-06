#pragma once

#include <vector>

namespace ao::schema {
struct SourceLocation {
    std::string file;
    size_t lineNumber;
    size_t col;
};

enum class ErrorCode {
    FAILED_TO_RESOLVE_IMPORT,
    CYCLICAL_IMPORT,
    SYNTAX_ERROR,
    MISSING_PACKAGE_DECLARATION,
    MULTIPLE_PACKAGE_DECLARATION,
    MULTIPLY_DEFINED_SYMBOL,
    INVALID_TYPE_ARGS,
    SYMBOL_NOT_DEFINED,
    SYMBOL_AMBIGUOUS,
    MULTIPLY_DEFINED_MESSAGE_ID,
    MULTIPLY_DEFINED_FIELD_ID,
    INVALID_VALUE_FOR_TYPE_PROPERTY,
    MULTIPLY_DEFINED_TYPE_PROPERTY,
    INTERNAL,
    OTHER,
};
struct Error {
    ErrorCode code;
    std::string message;
    SourceLocation loc;
};

struct ErrorContext {
    void require(bool condition, Error err) {
        if (condition)
            return;
        errors.push_back(err);
    }
    std::vector<Error> errors;
};
}  // namespace ao::schema