#pragma once

#include <format>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "Ast.h"
#include "Error.h"
#include "Frontend.h"

namespace ao::schema {
struct SymbolInfo {
    std::string qualifiedName;
    uint64_t id;
    SourceLocation defLoc;
};

struct SymbolTable {
    std::expected<SymbolInfo, Error> populateFromQualifiedId(
        std::string const& name,
        SourceLocation loc);
    std::optional<uint64_t> getQualifiedId(std::string const& name);

    uint64_t nextQualifiedIdName;
    std::unordered_map<std::string, std::pair<uint64_t, SourceLocation>>
        fullyQualifiedNameToId;
};

class CompilerContext {
   public:
    CompilerContext(CompilerFrontend& frontend) : m_frontend(&frontend) {}
    bool loadFile(std::string path);

    // Builds symbol table
    // Builds creates type dependency graph
    bool resolveSymbols();
    bool validateIds();

    // AstImport must be resolved here
    struct Module {
        std::string resolvedPath;
        std::shared_ptr<AstFile> ast;
        std::unordered_set<std::string> dependencies;
        std::unordered_map<std::string, SymbolInfo> exportedSymbols;
        std::unordered_map<uint64_t, AstMessage*> messagesById;
        AstQualifiedName packageName;
    };

    ErrorContext const& getErrorContext() const { return m_errors; }
    std::unordered_map<std::string, Module> const& getModules() const {
        return m_modules;
    }

   private:
    CompilerFrontend* m_frontend = nullptr;
    ErrorContext m_errors = {};
    std::unordered_map<std::string, Module> m_modules = {};
    SymbolTable m_symbolTable = {};
};
}  // namespace ao::schema