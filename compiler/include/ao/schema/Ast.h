#pragma once

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "Error.h"

namespace ao::schema {

enum class AstBaseType {
    INT,       // int k -> mirror C++ type
    UINT,      // uint k -> mirror C++ type
    ARRAY,     // Generate to std::vector
    OPTIONAL,  // Generate to std::optional
    ONEOF,     // Generate to std::variant
    USER,      // Used for messages, oneof, etc
};

struct AstQualifiedName {
    std::vector<std::string> name;
};

struct AstImport {
    std::string path;
    SourceLocation loc;
};

struct AstPackageDecl {
    AstQualifiedName name;
    SourceLocation loc;
};

struct AstTypeName {
    AstBaseType type;
    AstQualifiedName name;

    std::optional<uint64_t> width;
    std::optional<uint64_t> maxLen;

    std::vector<std::shared_ptr<AstTypeName>> subtypes;
    SourceLocation loc;
};

enum ValueLiteral {
    BOOLEAN,
    INT,
    NUMBER,
    STRING,
};
struct AstDirectiveValueLiteral {
    ValueLiteral type;
    std::string contents;
    SourceLocation loc;
};

enum class AstFieldDirectiveType {
    NET,
    CPP,
    FIELD,
    CUSTOM,
};
struct AstDirective {
    AstFieldDirectiveType type;
    std::string directiveName;
    std::unordered_map<std::string, ValueLiteral> properties;
    SourceLocation loc;
};

struct AstField {
    std::string name;
    uint64_t fieldNumber;
    AstTypeName typeName;

    std::vector<AstDirective> directives;
    SourceLocation loc;
};
struct AstFieldReserved {
    std::vector<uint64_t> fieldNumbers;
    SourceLocation loc;
};
struct AstDefault {
    std::vector<AstDirective> directives;
    SourceLocation loc;
};
struct AstFieldDecl {
    std::variant<AstField, AstFieldReserved, AstDefault> field;
    SourceLocation loc;
};
struct AstMessage {
    std::string name;
    std::optional<uint64_t> messageId;
    std::vector<AstFieldDecl> fields;
    SourceLocation loc;
};

struct AstDecl {
    std::variant<AstImport, AstPackageDecl, AstTypeName, AstMessage, AstDefault>
        decl;
    SourceLocation loc;
};

struct AstFile {
    std::vector<AstDecl> decls;

    // path to uniquely identify this file
    std::string absolutePath;
    SourceLocation loc;
};





}  // namespace ao::schema