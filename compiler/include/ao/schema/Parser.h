#pragma once
#include <string>
#include <string_view>

#include "ao/schema/Ast.h"

namespace ao::schema {
bool parseMatch(std::string_view str, std::string* errsOut = nullptr);
std::shared_ptr<AstFile> parseToAst(std::string_view path,
                                    std::string_view str,
                                    std::string* errsOut);

}  // namespace ao::schema
