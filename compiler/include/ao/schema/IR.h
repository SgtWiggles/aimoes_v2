#pragma once

#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "ao/schema/Ast.h"
#include "ao/schema/ResourceCache.h"
#include "ao/schema/SemanticContext.h"

namespace ao::schema::ir {
struct Type;

struct DirectiveValue {
    std::variant<bool, double, int64_t, uint64_t, IdFor<std::string>> value;
};

struct DirectiveProperty {
    IdFor<std::string> name;
    DirectiveValue value;
};

struct DirectiveProfile {
    enum ProfileKind {
        Disk,
        Net,
        Cpp,
        Lua,
        Field,
        Custom,
    };
    ProfileKind domain;
    IdFor<std::string> profileName;  // disk, net, cpp, lua, etc

    // key value pairs for the directives themselves
    // namespace=xyz for example
    std::vector<IdFor<DirectiveProperty>> directives;
};

struct DirectiveSet {
    std::vector<IdFor<DirectiveProfile>> directives;
};

struct Field {
    IdFor<std::string> name;
    uint64_t fieldNumber;
    IdFor<Type> type;

    IdFor<DirectiveSet> directives;
};

struct OneOf {
    std::vector<IdFor<Field>> arms;
};

struct Message {
    // Qualified name for debugging purposes
    std::string name;

    // Root level message id, these must be unique as per other passes
    std::optional<uint64_t> messageId;
    std::vector<IdFor<Field>> fields;

    IdFor<DirectiveSet> directives;
};
struct Scalar {
    enum ScalarKind : uint8_t {
        Bool,
        Int,   // signed
        UInt,  // unsigned
        F32,
        F64,
        String,
        Bytes,
    };
    ScalarKind kind;
    size_t width;
};

struct Array {
    IdFor<Type> type;
};

struct Optional {
    IdFor<Type> type;
};

struct Type {
    std::variant<Scalar, Array, Optional, IdFor<OneOf>, IdFor<Message>> payload;
};

struct IR {
    std::vector<std::string> strings;
    std::vector<DirectiveProfile> directiveProfiles;
    std::vector<DirectiveSet> directiveSets;
    std::vector<Field> fields;
    std::vector<Message> messages;
    std::vector<OneOf> oneOfs;
    std::vector<Type> types;
};

IR generateIR(
    std::unordered_map<std::string, ao::schema::SemanticContext::Module> const&
        modules);

}  // namespace ao::schema::ir