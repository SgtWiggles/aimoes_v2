#pragma once

#include <memory>
#include <string>

#include "Ast.h"

namespace ao::schema {
// This is the compiler frontend
class CompilerFrontend {
   public:
    virtual std::expected<std::shared_ptr<AstFile>, std::string> loadFile(
        std::string resolvedPath) = 0;

    // Returns path, module name must be fully resolved
    virtual std::expected<std::string, std::string> resolveModule(
        AstQualifiedName moduleName) = 0;
};
}  // namespace ao::schema