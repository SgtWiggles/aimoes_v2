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
    OTHER,
};
struct Error {
    ErrorCode code;
    std::string message;
    SourceLocation loc;
};

struct ErrorContext {
    std::vector<Error> errors;
};


}  // namespace ao::schema