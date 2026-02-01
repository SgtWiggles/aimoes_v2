#pragma once

#include "ao/schema/Ast.h"
#include "ao/schema/CompilerContext.h"

#include <expected>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
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
ao::schema::AstFieldDecl makeFieldDeclFromOneOf(
    ao::schema::AstFieldOneOf oneof);
ao::schema::AstFieldDecl makeFieldDeclReserved(
    std::vector<uint64_t> reservedIds);

ao::schema::AstField makeField(std::string const& name,
                               uint64_t number,
                               ao::schema::AstTypeName type);

ao::schema::AstFieldReserved makeReserved(std::vector<uint64_t> ids);
ao::schema::AstFieldOneOf makeOneOf(
    std::string const& name,
    uint64_t fieldNumber,
    std::vector<ao::schema::AstFieldDecl> innerFields = {});

// Type constructors
ao::schema::AstTypeName makeUserType(
    std::string const& qualifiedName,
    std::vector<std::shared_ptr<ao::schema::AstTypeName>> subtypes = {});

ao::schema::AstTypeName makeCtorType(
    ao::schema::AstBaseType base,
    std::vector<std::shared_ptr<ao::schema::AstTypeName>> subtypes = {});

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
