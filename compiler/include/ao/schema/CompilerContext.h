#pragma once

#include <string>
#include <unordered_map>

#include "Ast.h"
#include "Error.h"
#include "Frontend.h"

namespace ao::schema {
class CompilerContext {
   public:
    CompilerContext(CompilerFrontend& frontend) : m_frontend(&frontend) {}
    bool loadFile(std::string path);

    // AstImport must be resolved here
    struct Module {
        std::shared_ptr<AstFile> ast;
    };

    ErrorContext const& getErrorContext() const { return m_errors; }
    std::unordered_map<std::string, Module> const& getModules() const {
        return m_modules;
    }

   private:
    CompilerFrontend* m_frontend = nullptr;
    ErrorContext m_errors = {};
    std::unordered_map<std::string, Module> m_modules = {};
};
}  // namespace ao::schema