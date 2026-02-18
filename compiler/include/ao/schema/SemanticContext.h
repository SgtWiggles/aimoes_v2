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

    // pointer to declaration
    AstDecl const* decl;
};

// TODO maybe eventually change this to return pointers instead of integer ids?
struct SymbolTable {
    std::expected<SymbolInfo, Error> populateFromQualifiedId(
        std::string const& name,
        SourceLocation loc,
        AstDecl const* message);
    std::optional<uint64_t> getQualifiedId(std::string const& name);

    uint64_t nextQualifiedIdName;
    std::unordered_map<std::string, SymbolInfo> fullyQualifiedNameToId;
};

class SemanticContext {
   public:
    SemanticContext(CompilerFrontend& frontend) : m_frontend(&frontend) {}
    bool loadFile(std::string path);

    // Builds symbol table
    // Builds creates type dependency graph
    bool resolveSymbols();
    bool validateIds();
    bool computeDirectives();

    bool validate() {
        auto resolve = resolveSymbols();
        auto ids = validateIds();
        auto directives = computeDirectives();
        return resolve && ids && directives;
    }

    // AstImport must be resolved here
    struct Module {
        std::string resolvedPath;
        std::shared_ptr<AstFile> ast;
        std::unordered_set<std::string> dependencies;
        std::unordered_map<std::string, SymbolInfo> exportedSymbols;
        std::unordered_map<uint64_t, AstMessage*> messagesById;

        std::unordered_map<uint64_t, AstMessage const*> messagesBySymbolId;
        std::unordered_map<uint64_t, SymbolInfo> symbolInfoBySymbolId;

        AstQualifiedName packageName;
    };

    ErrorContext const& getErrorContext() const { return m_errors; }
    ErrorContext& getErrorContext() { return m_errors; }
    std::unordered_map<std::string, Module> const& getModules() const {
        return m_modules;
    }

    // Contains all global symbols
    SymbolTable const& getSymbolTable() const { return m_symbolTable; }

   private:
    CompilerFrontend* m_frontend = nullptr;
    ErrorContext m_errors = {};
    std::unordered_map<std::string, Module> m_modules = {};
    SymbolTable m_symbolTable = {};
};
}  // namespace ao::schema
