#pragma once

#include <string>

#include "Error.h"
namespace ao::schema {

enum class ValueLiteralType {
    BOOLEAN,
    INT,
    NUMBER,
    STRING,
};
struct AstValueLiteral {
    ValueLiteralType type;
    std::string contents;
    SourceLocation loc;
};
}
