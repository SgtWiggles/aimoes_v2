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
#include "ao/schema/Serializer.h"

#include <ao/pack/HashingStream.h>

#include "ao/meta/Reflect.h"

#include "ao/utils/Blake3Hasher.h"

// We need to maybe make this incremental?
// That means the IR generate context needs to be there
// Given a base IR, add new types from an AST
namespace ao::schema::ir {
struct Type;
struct IRHeader {
    // aosl in hex
    uint32_t magic = 0x616f736c;
    uint64_t version = 2;

    auto operator<=>(IRHeader const& other) const = default;
};

struct DirectiveValue {
    using Value =
        std::variant<bool, double, int64_t, uint64_t, IdFor<std::string>>;
    AO_MEMBER(Value, value);
    auto operator<=>(DirectiveValue const& other) const = default;
};
inline size_t hash_value(DirectiveValue const& value) {
    size_t ret = 0;
    boost::hash_combine(ret, value.value);
    return ret;
}

struct DirectiveProperty {
    AO_MEMBER(IdFor<std::string>, name);
    AO_MEMBER(DirectiveValue, value);
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

        ProfileKindCount,
    };
    AO_MEMBER(ProfileKind, domain);
    AO_MEMBER(IdFor<std::string>, profileName) = {
    };  // disk, net, cpp, lua, etc

    // key value pairs for the directives themselves
    // namespace=xyz for example
    // For consistency these should be sorted!!
    // These should be sorted by directive name
    AO_MEMBER(std::vector<IdFor<DirectiveProperty>>, properties) = {};
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
    AO_MEMBER(std::vector<IdFor<DirectiveProfile>>, directives);
    auto operator<=>(DirectiveSet const& other) const = default;
};
inline size_t hash_value(DirectiveSet const& set) {
    size_t base = 0;
    boost::hash_combine(base, set.directives);
    return base;
}

struct Field {
    AO_MEMBER(IdFor<std::string>, name);
    AO_MEMBER(uint64_t, fieldNumber);
    AO_MEMBER(IdFor<Type>, type);
    AO_MEMBER(IdFor<DirectiveSet>, directives);
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
    AO_MEMBER(std::vector<IdFor<Field>>, arms);
    auto operator<=>(OneOf const& other) const = default;
};
inline size_t hash_value(OneOf const& field) {
    size_t base = 0;
    boost::hash_combine(base, field.arms);
    return base;
}

struct Message {
    // Qualified name
    AO_MEMBER(IdFor<std::string>, name);

    // Idempotency key for this symbol, already used everywhere!
    AO_MEMBER(uint64_t, symbolId);

    // Root level message id, these must be unique as per other passes
    AO_MEMBER(std::optional<uint64_t>, messageNumber);
    AO_MEMBER(std::vector<IdFor<Field>>, fields);

    AO_MEMBER(IdFor<DirectiveSet>, directives);

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

        // Special types for bytes/strings
        CHAR,
        BYTE,

        ScalarMax,
    };
    // TODO maybe change this to a variant? nothing directly consumes it other
    // than the compiler itself
    AO_MEMBER(ScalarKind, kind);
    AO_MEMBER(size_t, width) = 0;
    auto operator<=>(Scalar const& other) const = default;
};
inline size_t hash_value(Scalar const& scalar) {
    size_t ret = 0;
    boost::hash_combine(ret, scalar.kind);
    boost::hash_combine(ret, scalar.width);
    return ret;
}

struct Array {
    AO_MEMBER(IdFor<Type>, type);
    AO_MEMBER(std::optional<uint64_t>, minSize);
    AO_MEMBER(std::optional<uint64_t>, maxSize);

    auto operator<=>(Array const& other) const = default;
};
inline size_t hash_value(Array const& scalar) {
    size_t ret = 0;
    boost::hash_combine(ret, scalar.type);
    boost::hash_combine(ret, scalar.maxSize);
    return ret;
}

struct Optional {
    AO_MEMBER(IdFor<Type>, type);
    auto operator<=>(Optional const& other) const = default;
};
inline size_t hash_value(Optional const& scalar) {
    return hash_value(scalar.type);
}

struct EnumField {
    AO_MEMBER(int64_t, fieldNumber);
    AO_MEMBER(IdFor<std::string>, name);
    auto operator<=>(EnumField const& other) const = default;
};
inline size_t hash_value(EnumField const& v) {
    size_t ret = 0;
    boost::hash_combine(ret, boost::hash_value(v.fieldNumber));
    boost::hash_combine(ret, hash_value(v.name));
    return ret;
}

struct Enum {
    AO_MEMBER(IdFor<std::string>, name);
    AO_MEMBER(std::vector<IdFor<EnumField>>, fields);
    AO_MEMBER(IdFor<DirectiveSet>, directives);
    auto operator<=>(Enum const& other) const = default;
};
inline size_t hash_value(Enum const& e) {
    size_t ret = 0;
    boost::hash_combine(ret, e.name);
    boost::hash_combine(ret, e.fields);
    boost::hash_combine(ret, e.directives);
    return ret;
}

struct Type {
    using Value = std::variant<Scalar,
                               Array,
                               Optional,
                               IdFor<OneOf>,
                               IdFor<Message>,
                               IdFor<Enum>>;
    AO_MEMBER(Value, payload);
    auto operator<=>(Type const& other) const = default;
};
inline size_t hash_value(Type const& type) {
    return boost::hash_value(type.payload);
}

struct Module {
    AO_MEMBER(IdFor<std::string>, moduleName);
    AO_MEMBER(std::vector<IdFor<Message>>, messages);
    AO_MEMBER(std::vector<IdFor<Enum>>, enums);
    auto operator<=>(Module const& other) const = default;
};
inline size_t hash_value(Module const& m) {
    size_t ret = 0;
    boost::hash_combine(ret, m.moduleName);
    boost::hash_combine(ret, m.messages);
    return ret;
}

struct IR {
    AO_MEMBER(std::vector<std::string>, strings);
    AO_MEMBER(std::vector<DirectiveProperty>, directiveProperties);
    AO_MEMBER(std::vector<DirectiveProfile>, directiveProfiles);
    AO_MEMBER(std::vector<DirectiveSet>, directiveSets);
    AO_MEMBER(std::vector<OneOf>, oneOfs);
    AO_MEMBER(std::vector<Field>, fields);
    AO_MEMBER(std::vector<Message>, messages);
    AO_MEMBER(std::vector<Type>, types);
    AO_MEMBER(std::vector<Module>, modules);

    AO_MEMBER(std::vector<Enum>, enums);
    AO_MEMBER(std::vector<EnumField>, enumFields);
};

IR generateIR(
    std::unordered_map<std::string, ao::schema::SemanticContext::Module> const&
        modules,
    ErrorContext& errors);

template <class Stream>
bool serializeIRFile(Stream& stream, IR const& ir) {
    IRHeader header{};
    ao::schema::serialize(stream, header);
    if (!stream.ok())
        return false;
    ao::pack::HashingStream<Stream, utils::hash::Blake3Hasher> hashingStream(
        stream);
    hashingStream.enableHashing();
    ao::schema::serialize(hashingStream, ir);
    if (!stream.ok())
        return false;
    auto computedHash = hashingStream.digest();
    ao::schema::serialize(stream, computedHash);
    return stream.ok();
}

template <class Stream>
bool deserializeIRFile(Stream& stream, IR& out) {
    IRHeader header;
    ao::schema::deserialize(stream, header);
    if (!stream.ok())
        return false;

    out = IR{};

    ao::pack::HashingStream<Stream, utils::hash::Blake3Hasher> hashingStream(
        stream);
    hashingStream.enableHashing();
    ao::schema::deserialize(hashingStream, out);
    if (!stream.ok())
        return false;
    auto computedHash = hashingStream.digest();

    utils::hash::Blake3Hasher::Hash expectedHash = {};
    ao::schema::deserialize(stream, expectedHash);
    if (!stream.ok())
        return false;

    stream.require(computedHash == expectedHash, ao::pack::Error::BadData);
    return stream.ok();
}

}  // namespace ao::schema::ir

namespace ao::schema {
template <>
struct Serializer<ir::IRHeader> {
    template <class Stream>
    void serialize(Stream& stream, ir::IRHeader const& prop) {
        Serializer<uint32_t>{}.serialize(stream, prop.magic);
        Serializer<uint64_t>{}.serialize(stream, prop.version);
    }
    template <class Stream>
    void deserialize(Stream& stream, ir::IRHeader& prop) {
        Serializer<uint32_t>{}.deserialize(stream, prop.magic);
        Serializer<uint64_t>{}.deserialize(stream, prop.version);
        stream.require(prop == ir::IRHeader{}, ao::pack::Error::BadData);
    }
};
template <>
struct Serializer<ir::DirectiveProfile::ProfileKind>
    : public EnumSerializer<ir::DirectiveProfile::ProfileKind,
                            ir::DirectiveProfile::ProfileKindCount> {
    using EnumSerializer::EnumSerializer;
};
template <>
struct Serializer<ir::Scalar::ScalarKind>
    : public EnumSerializer<ir::Scalar::ScalarKind, ir::Scalar::ScalarMax> {
    using EnumSerializer::EnumSerializer;
};
}  // namespace ao::schema
