#pragma once

#include <expected>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "AstBaseType.h"
#include "AstValueLiteral.h"
#include "Error.h"

namespace ao::schema {
using ResolvedTypeId = uint64_t;

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
    bool operator==(AstQualifiedName const& other) const {
        return name == other.name;
    }
};

AstQualifiedName parseQualifiedName(std::string_view path);

struct AstImport {
    AstQualifiedName moduleName;
    SourceLocation loc;
};

struct AstPackageDecl {
    AstQualifiedName name;
    SourceLocation loc;
};

enum class AstFieldDirectiveType {
    NET,
    CPP,
    FIELD,
    CUSTOM,
};

struct AstFieldDecl;
struct AstMessageBlock {
    std::vector<AstFieldDecl> fields;
    // points into the local fields, computed later
    std::unordered_map<uint64_t, AstFieldDecl*> fieldsByFieldId;
    SourceLocation loc;
};

struct NormalizedAstTypeProperties {
    AstNormalizedTypeProperties props;
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

    std::optional<NormalizedAstTypeProperties> normalizedProperties;
};

struct AstDirective {
    AstFieldDirectiveType type;
    std::string directiveName;
    std::vector<std::pair<std::string, AstValueLiteral>> properties;
    SourceLocation loc;
};

struct AstDirectiveBlock {
    std::vector<AstDirective> directives;
    std::unordered_map<std::string,
                       std::vector<std::pair<std::string, AstValueLiteral>>>
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

struct AstEnumReserved {
    std::vector<int64_t> fieldNumbers;
    SourceLocation loc;
};
struct AstEnumValue {
    int64_t fieldNumber;
    std::string name;
    SourceLocation loc;

    uint64_t symbolId;
};
struct AstEnumDecl {
    std::variant<AstEnumValue, AstEnumReserved> entry;
    SourceLocation loc;
};

struct AstEnumBlock {
    std::vector<AstEnumDecl> decls;
    SourceLocation loc;

    std::unordered_map<int64_t, AstEnumDecl*> fieldsByFieldId;
};

struct AstEnum {
    std::string name;
    AstEnumBlock block;
    AstDirectiveBlock directives;

    SourceLocation loc;
    uint64_t symbolId;
};

struct AstDecl {
    std::variant<AstImport, AstPackageDecl, AstMessage, AstDefault, AstEnum>
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