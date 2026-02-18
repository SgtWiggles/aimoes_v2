#pragma once

#include <format>
#include <vector>
#include <sstream>

namespace ao::schema {
struct SourceLocation {
    std::string file;
    size_t line;
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
    UNKNOWN_TYPE_PROPERTY,
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
        fail(std::move(err));
    }
    void fail(Error err) { errors.push_back(err); }
    std::string generateErrorText() const {
        std::stringstream ss;
        for (auto const& error : errors) {
            ss << "\n" << std::format("{}: {}", error.loc, error.message);
        }
        return ss.str();
    }

    std::vector<Error> errors;
};
}  // namespace ao::schema

template <>
struct std::formatter<ao::schema::SourceLocation> {
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }
    auto format(ao::schema::SourceLocation const& p,
                std::format_context& ctx) const {
        return std::format_to(ctx.out(), "{}:{}:{}", p.file, p.line, p.col);
    }
};
