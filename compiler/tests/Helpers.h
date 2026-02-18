#pragma once

#include "ao/schema/Ast.h"
#include "ao/schema/IR.h"
#include "ao/schema/SemanticContext.h"

#include <expected>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Helper utilities used by tests.
ao::schema::AstQualifiedName qnameFromString(std::string const& s);
ao::schema::SourceLocation locFor(std::string const& path);

std::shared_ptr<ao::schema::AstFile> makeFileWithPackageAndDecls(
    std::string absolutePath,
    std::optional<std::string> packageName,
    std::vector<ao::schema::AstDecl> decls,
    std::vector<std::string> imports = {});

// Message helpers
ao::schema::AstMessage makeMessage(
    std::string const& name,
    std::vector<ao::schema::AstFieldDecl> fields = {},
    std::optional<uint64_t> messageId = {});

// Field / type helpers
ao::schema::AstFieldDecl makeFieldDecl(ao::schema::AstField field);
ao::schema::AstFieldDecl makeFieldDeclReserved(
    std::vector<uint64_t> reservedIds);

ao::schema::AstField makeField(std::string const& name,
                               uint64_t number,
                               ao::schema::AstType type);

ao::schema::AstFieldReserved makeReserved(std::vector<uint64_t> ids);

// Type constructors
ao::schema::AstType makeUserType(
    std::string const& qualifiedName,
    std::vector<std::shared_ptr<ao::schema::AstType>> subtypes = {});

ao::schema::AstType makeCtorType(
    ao::schema::AstBaseType base,
    std::vector<std::shared_ptr<ao::schema::AstType>> subtypes = {});

// Directive helpers
ao::schema::AstValueLiteral makeStrLit(std::string const& s);
ao::schema::AstDirective makeDirective(
    std::string const& directiveName,
    std::vector<std::pair<std::string, std::string>> properties);
ao::schema::AstDirectiveBlock makeDirectiveBlock(
    std::vector<ao::schema::AstDirective> directives);
ao::schema::AstDecl makeDefaultDeclWithDirectiveBlock(
    ao::schema::AstDirectiveBlock block);

std::optional<ao::schema::ir::IR> buildToIR(std::string_view fileContents,
                                            std::string& errs);


// Minimal test frontend that resolves by identity and returns provided
// AstFiles.
class SimpleTestFrontend : public ao::schema::CompilerFrontend {
   public:
    std::unordered_map<std::string, std::shared_ptr<ao::schema::AstFile>>
        resolvedModules;

    virtual std::expected<std::string, std::string> resolvePath(
        std::string /*currentFile*/,
        std::string path) override;

    virtual std::expected<std::shared_ptr<ao::schema::AstFile>, std::string>
    loadFile(std::string resolvedPath) override;
};
