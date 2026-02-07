#pragma once

#include <boost/container_hash/hash.hpp>
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
    auto operator<=>(DirectiveValue const& other) const = default;
};
inline size_t hash_value(DirectiveValue const& value) {
    size_t ret = 0;
    boost::hash_combine(ret, value.value);
    return ret;
}

struct DirectiveProperty {
    IdFor<std::string> name;
    DirectiveValue value;
    auto operator<=>(DirectiveProperty const& other) const = default;
};
inline size_t hash_value(DirectiveProperty const& prop) {
    size_t ret = 0;
    boost::hash_combine(ret, prop.name);
    boost::hash_combine(ret, prop.value);
    return ret;
}

struct DirectiveProfile {
    enum ProfileKind {
        Disk,
        Net,
        Cpp,
        Lua,
        Field,
        Message,
        Custom,
    };
    ProfileKind domain;
    IdFor<std::string> profileName = {};  // disk, net, cpp, lua, etc

    // key value pairs for the directives themselves
    // namespace=xyz for example
    // For consistency these should be sorted!!
    // These should be sorted by directive name
    std::vector<IdFor<DirectiveProperty>> properties = {};
    auto operator<=>(DirectiveProfile const& other) const = default;
};
inline size_t hash_value(DirectiveProfile const& profile) {
    size_t base = 0;
    boost::hash_combine(base, (size_t)profile.domain);
    boost::hash_combine(base, profile.profileName);
    boost::hash_combine(base, profile.properties);
    return base;
}

struct DirectiveSet {
    // Sort by domain!
    std::vector<IdFor<DirectiveProfile>> directives;
    auto operator<=>(DirectiveSet const& other) const = default;
};
inline size_t hash_value(DirectiveSet const& set) {
    size_t base = 0;
    boost::hash_combine(base, set.directives);
    return base;
}

struct Field {
    IdFor<std::string> name;
    uint64_t fieldNumber;
    IdFor<Type> type;

    IdFor<DirectiveSet> directives;
    auto operator<=>(Field const& other) const = default;
};
inline size_t hash_value(Field const& field) {
    size_t base = 0;
    boost::hash_combine(base, field.name);
    boost::hash_combine(base, field.fieldNumber);
    boost::hash_combine(base, field.type);
    boost::hash_combine(base, field.directives);
    return base;
}

struct OneOf {
    std::vector<IdFor<Field>> arms;
    auto operator<=>(OneOf const& other) const = default;
};
inline size_t hash_value(OneOf const& field) {
    size_t base = 0;
    boost::hash_combine(base, field.arms);
    return base;
}

struct Message {
    // Qualified name for debugging purposes
    IdFor<std::string> name;

    // Idempotency key for this symbol, already used everywhere!
    uint64_t symbolId;

    // Root level message id, these must be unique as per other passes
    std::optional<uint64_t> messageNumber;
    std::vector<IdFor<Field>> fields;

    IdFor<DirectiveSet> directives;

    // Compare by symbol id only, the rest are just properties we are passing
    // through
    auto operator<=>(Message const& other) const {
        return symbolId <=> other.symbolId;
    }
};

struct Scalar {
    enum ScalarKind : uint8_t {
        BOOL,
        INT,   // signed
        UINT,  // unsigned
        F32,
        F64,
    };
    ScalarKind kind;
    size_t width = 0;
    // TODO varint encoding
    // bool varintEncoded = false;
    auto operator<=>(Scalar const& other) const = default;
};
inline size_t hash_value(Scalar const& scalar) {
    size_t ret = 0;
    boost::hash_combine(ret, scalar.kind);
    boost::hash_combine(ret, scalar.width);
    return ret;
}

struct Array {
    IdFor<Type> type;
    std::optional<int> minSize;
    std::optional<int> maxSize;

    auto operator<=>(Array const& other) const = default;
    
};
inline size_t hash_value(Array const& scalar) {
    size_t ret = 0;
    boost::hash_combine(ret, scalar.type);
    boost::hash_combine(ret, scalar.maxSize);
    return ret;
}

struct Optional {
    IdFor<Type> type;
    auto operator<=>(Optional const& other) const = default;
};
inline size_t hash_value(Optional const& scalar) {
    return hash_value(scalar.type);
}

struct Type {
    std::variant<Scalar, Array, Optional, IdFor<OneOf>, IdFor<Message>> payload;
    auto operator<=>(Type const& other) const = default;
};
inline size_t hash_value(Type const& type) {
    return boost::hash_value(type.payload);
}

struct IR {
    std::vector<std::string> strings;
    std::vector<DirectiveProperty> directiveProperties;
    std::vector<DirectiveProfile> directiveProfiles;
    std::vector<DirectiveSet> directiveSets;
    std::vector<OneOf> oneOfs;
    std::vector<Field> fields;
    std::vector<Message> messages;
    std::vector<Type> types;
};

IR generateIR(
    std::unordered_map<std::string, ao::schema::SemanticContext::Module> const&
        modules);
}  // namespace ao::schema::ir


