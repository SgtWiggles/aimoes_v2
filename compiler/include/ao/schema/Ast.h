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
    F32,       // float
    F64,       // double

    STRING,    // strings
    BYTES,     // byte array (still a string)

    ARRAY,     // Generate to std::vector
    OPTIONAL,  // Generate to std::optional
    ONEOF,     // Generate to std::variant
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

enum class AstFieldDirectiveType {
    NET,
    CPP,
    FIELD,
    CUSTOM,
};
struct AstTypeProperty {
    std::string name;
    AstValueLiteral value;
};

struct AstTypeProperties {
    std::vector<AstTypeProperty> props;
};

struct AstFieldDecl;
struct AstMessageBlock {
    std::vector<AstFieldDecl> fields;
    // points into the local fields, computed later
    std::unordered_map<uint64_t, AstFieldDecl*> fieldsByFieldId;
    SourceLocation loc;
};

struct AstType {
    AstBaseType type;
    AstQualifiedName name;

    std::vector<std::shared_ptr<AstType>> subtypes;  // used in parametric types
    AstTypeProperties properties;                    // used in all types
    AstMessageBlock block;                           // used in one of

    SourceLocation loc;

    // Types for resolving the names to their IDS
    std::optional<ResolvedTypeId> resolvedDef;
    std::optional<std::string> resolvedFqn;
};

struct AstDirective {
    AstFieldDirectiveType type;
    std::string directiveName;
    std::unordered_map<std::string, AstValueLiteral> properties;
    SourceLocation loc;
};

struct AstDirectiveBlock {
    std::vector<AstDirective> directives;
    std::unordered_map<std::string,
                       std::unordered_map<std::string, AstValueLiteral>>
        effectiveDirectives;
};

struct AstField {
    std::string name;
    uint64_t fieldNumber;
    AstType typeName;

    AstDirectiveBlock directives;
    SourceLocation loc;
};

struct AstFieldReserved {
    std::vector<uint64_t> fieldNumbers;
    SourceLocation loc;
};
struct AstDefault {
    AstDirectiveBlock directives;
    SourceLocation loc;
};
struct AstFieldDecl {
    std::variant<AstField, AstFieldReserved, AstDefault> field;
    SourceLocation loc;
};
struct AstMessage {
    std::string name;
    std::optional<uint64_t> messageId;
    AstMessageBlock block;
    AstDirectiveBlock directives;
    SourceLocation loc;

    uint64_t symbolId;
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