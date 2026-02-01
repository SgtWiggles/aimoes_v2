#pragma once

#include <expected>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "Error.h"

namespace ao::schema {
using ResolvedTypeId = uint64_t;

enum class AstBaseType {
    INT,       // int k -> mirror C++ type
    UINT,      // uint k -> mirror C++ type
    ARRAY,     // Generate to std::vector
    OPTIONAL,  // Generate to std::optional
    USER,      // Used for messages, oneof, etc
};

struct AstQualifiedName {
    std::vector<std::string> name;
    std::string toString() const {
        std::stringstream ss;
        bool needsDot = false;
        for (auto const& section : name) {
            if (needsDot)
                ss << ".";
            ss << section;
            needsDot = true;
        }
        return ss.str();
    }
    std::string qualifyName(std::string const& v) const {
        std::stringstream ss;
        bool needsDot = false;
        for (auto const& section : name) {
            if (needsDot)
                ss << ".";
            ss << section;
            needsDot = true;
        }

        if (needsDot) {
            ss << ".";
        }
        ss << v;
        return ss.str();
    }
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

    // Types for resolving the names to their IDS
    std::optional<ResolvedTypeId> resolvedDef;
    std::optional<std::string> resolvedFqn;
};

enum ValueLiteralType {
    BOOLEAN,
    INT,
    NUMBER,
    STRING,
};
struct AstDirectiveValueLiteral {
    ValueLiteralType type;
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
    std::unordered_map<std::string, AstDirectiveValueLiteral> properties;
    SourceLocation loc;
};

struct AstFieldDecl;

struct AstField {
    std::string name;
    uint64_t fieldNumber;
    AstTypeName typeName;

    std::vector<AstDirective> directives;
    SourceLocation loc;
};

struct AstMessageBlock {
    std::vector<AstFieldDecl> fields;
    // points into the local fields, computed later
    std::unordered_map<uint64_t, AstFieldDecl*> fieldsByFieldId;
    SourceLocation loc;
};

struct AstFieldOneOf {
    std::string name;
    uint64_t fieldNumber;

    std::vector<AstDirective> directives;

    AstMessageBlock block;
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
    std::variant<AstField, AstFieldOneOf, AstFieldReserved, AstDefault> field;
    SourceLocation loc;
};
struct AstMessage {
    std::string name;
    std::optional<uint64_t> messageId;
    AstMessageBlock block;
    SourceLocation loc;
};

struct AstDecl {
    std::variant<AstImport, AstPackageDecl, AstMessage, AstDefault> decl;
    SourceLocation loc;
};

struct AstFile {
    std::vector<AstDecl> decls;

    // path to uniquely identify this file
    std::string absolutePath;
    SourceLocation loc;
};

}  // namespace ao::schema