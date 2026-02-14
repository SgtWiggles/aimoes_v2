#pragma once

#include <string>
#include <variant>

#include "AstValueLiteral.h"
#include "Error.h"

namespace ao::schema {
enum class AstBaseType {
    BOOL,
    INT,   // int k -> mirror C++ type
    UINT,  // uint k -> mirror C++ type
    F32,   // float
    F64,   // double

    STRING,  // strings
    BYTES,   // byte array (still a string)

    ARRAY,     // Generate to std::vector
    OPTIONAL,  // Generate to std::optional
    ONEOF,     // Generate to std::variant
    USER,      // Used for messages, oneof, etc

    TOTAL_AST_BASE_TYPES
};

struct AstTypeProperty {
    std::string name;
    AstValueLiteral value;
    SourceLocation loc;
};

struct AstTypeProperties {
    std::vector<AstTypeProperty> props;
    SourceLocation loc;
};

template <AstBaseType>
struct AstBaseTypeProperties;

namespace detail {
void failUnknownProperty(ErrorContext& errs, AstTypeProperty const& prop);
struct EmptyParseProperties {
    bool parseProperties(ErrorContext& errs, AstTypeProperties const& props);
};
struct IntParseProperties {
    bool parseProperties(ErrorContext& errs, AstTypeProperties const& props);
    enum Encoding {
        ZIGZAG_VARINT,
        FIXED,
    };
    // bits = 0 means varint
    unsigned bits = 0;
    Encoding encoding = ZIGZAG_VARINT;
};

// TODO add parsing for these later
struct FloatParseProperties : EmptyParseProperties {};
struct ArrayParseProperties : EmptyParseProperties {};

}  // namespace detail

template <>
struct AstBaseTypeProperties<AstBaseType::BOOL> : detail::EmptyParseProperties {
};
template <>
struct AstBaseTypeProperties<AstBaseType::USER> : detail::EmptyParseProperties {
};
template <>
struct AstBaseTypeProperties<AstBaseType::OPTIONAL>
    : detail::EmptyParseProperties {};
template <>
struct AstBaseTypeProperties<AstBaseType::ONEOF>
    : detail::EmptyParseProperties {};

template <>
struct AstBaseTypeProperties<AstBaseType::UINT> : detail::IntParseProperties {};
template <>
struct AstBaseTypeProperties<AstBaseType::INT> : detail::IntParseProperties {};

template <>
struct AstBaseTypeProperties<AstBaseType::F32> : detail::FloatParseProperties {
};
template <>
struct AstBaseTypeProperties<AstBaseType::F64> : detail::FloatParseProperties {
};

template <>
struct AstBaseTypeProperties<AstBaseType::STRING>
    : detail::ArrayParseProperties {};
template <>
struct AstBaseTypeProperties<AstBaseType::BYTES>
    : detail::ArrayParseProperties {};
template <>
struct AstBaseTypeProperties<AstBaseType::ARRAY>
    : detail::ArrayParseProperties {};

namespace detail {
template <size_t Idx>
AstBaseTypeProperties<(AstBaseType)Idx> getBaseTypeProperties() {
    return {};
}
template <size_t... Idx>
using ParsedTypeProperties =
    std::variant<decltype(getBaseTypeProperties<Idx>())...>;
template <size_t... Idx>
auto getPropertiesVariant(std::index_sequence<Idx...>) {
    return ParsedTypeProperties<Idx...>{};
}
}  // namespace detail

using AstNormalizedTypeProperties = decltype(detail::getPropertiesVariant(
    std::make_index_sequence<(size_t)AstBaseType::TOTAL_AST_BASE_TYPES>()));
AstNormalizedTypeProperties parseTypeProperties(ErrorContext& errs,
                                                AstBaseType baseType,
                                                AstTypeProperties const& props);

}  // namespace ao::schema