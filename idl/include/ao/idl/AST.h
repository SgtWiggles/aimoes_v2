#pragma once

#include <string>
#include <variant>
#include <vector>

namespace ao::idl {
enum class ImportKind { Relative, Absolute };
struct ImportPath {
    ImportKind kind;
    std::string text;
};

struct Namespace {
  std::vector<std::string> parts;   // ["game","components"]
};
enum class DeclarationKind {
    Message,
    Component,
    Command,
    Event,
    Sum,
    NetFormat,
    Enum,
    Rpc
};

struct Span {
    int line0, col0, line1, col1;
};

struct Ident {
    std::string text;
    Span span;
};
struct QualifiedName {
    std::vector<Ident> parts;
    Span span;
};

struct TypeRef {
    struct Builtin {
        Ident name;
    };  // "i32", "u16", "string", "bytes"
    struct Named {
        QualifiedName name;
    };  // "common.EntityId", "TransformQ"

    struct Optional {
        std::unique_ptr<TypeRef> inner;
    };  // opt<T>
    struct Array {
        std::unique_ptr<TypeRef> inner;
    };  // repeated T
    // If you want: map<K,V>, fixed_array<T,N>, etc.

    std::variant<Builtin, Named, Optional, Array> node;
    Span span;
};

using Literal = std::variant<int64_t, double, bool, std::string>;
struct Expr {
    // Minimal: just literals and arrays, plus name refs.
    // You can extend later (dicts, enums, etc.).
    struct NameRef {
        QualifiedName name;
    };
    struct Array {
        std::vector<Expr> items;
    };
    struct Object {
        std::vector<std::pair<Ident, Expr>> kv;
    };

    std::variant<Literal, NameRef, Array, Object> node;
    Span span;
};
struct Attribute {
    Ident key;   // default, max_len, since, bits, step, etc.
    Expr value;  // could be literal or object
};

struct Field {
    TypeRef type;
    Ident name;
    uint32_t fieldId;
    std::vector<Attribute> attrs;
    Span span;
};

enum class MessageKind { Message, Component, Command, Event };
struct Message {
    MessageKind kind;
    Ident name;
    std::optional<uint32_t> typeId;
    std::vector<Field> fields;
    std::vector<Attribute> policies;

    Span span;
};

struct EnumValue {
    Ident name;
    int64_t value;
    Span span;
};

struct Enum {
    Ident name;
    uint32_t typeId;
    std::vector<EnumValue> values;
    Span span;
};
struct SumCase {
    Ident name;
    uint32_t tag;               // required
    std::vector<Field> fields;  // field IDs local to the case
    Span span;
};

struct Sum {
    Ident name;
    uint32_t typeId;
    std::vector<SumCase> cases;
    Span span;
};

struct DeltaRule {
    // e.g. delta: changed_mask(fields=[x,y,z,yaw]);
    Ident mode;                         // "changed_mask"
    std::vector<QualifiedName> fields;  // supports paths like pos.x
    Span span;
};

struct FieldEncoding {
    // e.g. x: quant(step=0.02, bits=18);
    QualifiedName fieldPath;     // can be simple "x" or "pos.x"
    Ident encoding;               // "quant", "bits", "varint", "optional", ...
    std::vector<Attribute> args;  // step=..., bits=...
    Span span;
};

struct NetProfile {
    Ident name;                // "RepTransform"
    QualifiedName targetType;  // "TransformQ"
    uint32_t profileId;

    std::optional<DeltaRule> delta;
    std::vector<FieldEncoding> encodings;

    Span span;
};

struct RpcStream {
  Ident direction;                 // request/response/bidi
  Ident payload_kind;              // bytes or a type name (optional)
  std::vector<Attribute> opts;  // stream_max_len, chunk_max, checksum...
};

struct Rpc {
  Ident name;
  uint32_t rpc_id;

  std::vector<Attribute> opts;  // direction:c2s, reliable:true, etc.

  QualifiedName request_type;
  QualifiedName response_type;

  std::optional<RpcStream> stream;

  Span span;
};

struct TypeAlias {
    Ident name;
    QualifiedName type;
};

using Decl = std::variant<TypeAlias, Enum, Message, Sum, NetProfile, Rpc>;

struct Module {
  std::string path;                 // "game/components/transform.idl"
  std::vector<ImportPath> imports;
  Namespace ns;
  std::vector<Decl> decls;
};
}  // namespace ao::idl
