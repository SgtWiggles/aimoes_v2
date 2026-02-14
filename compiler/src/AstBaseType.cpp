#include "ao/schema/AstBaseType.h"

namespace ao::schema {

namespace detail {
void failUnknownProperty(ErrorContext& errs, AstTypeProperty const& prop) {
    errs.fail({
        .code = ErrorCode::UNKNOWN_TYPE_PROPERTY,
        .message = std::format("Unknown type property: {}", prop.name),
        .loc = prop.loc,
    });
}

bool EmptyParseProperties::parseProperties(ErrorContext& errs,
                                           AstTypeProperties const& props) {
    if (props.props.size() == 0)
        return true;
    for (auto const& prop : props.props) {
        failUnknownProperty(errs, prop);
    }
    return false;
}

bool getParseProperties(ErrorContext& errs,
                        AstValueLiteral const& literal,
                        unsigned& out) {
    if (literal.type != ValueLiteralType::INT) {
        errs.fail({
            .code = ErrorCode::INVALID_VALUE_FOR_TYPE_PROPERTY,
            .message = std::format("Expected integer literal"),
            .loc = literal.loc,
        });
        return false;
    }
    out = (unsigned)std::stoi(literal.contents);
    return true;
}
bool getParseProperties(ErrorContext& errs,
                        AstValueLiteral const& literal,
                        std::string& out) {
    if (literal.type != ValueLiteralType::STRING) {
        errs.fail({
            .code = ErrorCode::INVALID_VALUE_FOR_TYPE_PROPERTY,
            .message = std::format("Expected string literal"),
            .loc = literal.loc,
        });
        return false;
    }
    out = literal.contents;
    return true;
}

bool IntParseProperties::parseProperties(ErrorContext& errs,
                                         AstTypeProperties const& props) {
    bool setZigZag = false;
    bool success = true;
    for (auto const& prop : props.props) {
        if (prop.name == "bits") {
            getParseProperties(errs, prop.value, this->bits);
        } else if (prop.name == "encoding") {
            setZigZag = true;
            std::string tmp;

            if (!getParseProperties(errs, prop.value, tmp))
                continue;
            if (tmp == "zigzag") {
                this->encoding = ZIGZAG_VARINT;
            } else if (tmp == "fixed") {
                this->encoding = FIXED;
            } else {
                errs.fail({
                    .code = ErrorCode::INVALID_VALUE_FOR_TYPE_PROPERTY,
                    .message =
                        std::format(R"(Expected one of: "zigzag", "fixed")"),
                    .loc = prop.loc,
                });
                success = false;
            }
        } else {
            failUnknownProperty(errs, prop);
            success = false;
        }
    }
    if (!setZigZag && bits > 0)
        this->encoding = FIXED;

    if (this->encoding == ZIGZAG_VARINT && bits != 0) {
        errs.fail({
            .code = ErrorCode::INVALID_VALUE_FOR_TYPE_PROPERTY,
            .message = std::format("Cannot set bits > 0 with varint encoding"),
            .loc = props.loc,

        });
        success = false;
    } else if (this->encoding == FIXED && bits == 0) {
        errs.fail({
            .code = ErrorCode::INVALID_VALUE_FOR_TYPE_PROPERTY,
            .message = std::format("Cannot set bits == 0 with fixed encoding"),
            .loc = props.loc,
        });
        success = false;
    }

    return success;
}
}  // namespace detail

template <size_t Idx>
void parseTypePropertiesSingle(ErrorContext& errs,
                               AstBaseType baseType,
                               AstTypeProperties const& props,
                               AstNormalizedTypeProperties& out) {
    if ((size_t)baseType != Idx)
        return;
    AstBaseTypeProperties<(AstBaseType)Idx> normalized;
    if (normalized.parseProperties(errs, props)) {
        out = std::move(normalized);
    } else {
        // Ensure this is filled but default initialized
        out = AstNormalizedTypeProperties{
            AstBaseTypeProperties<(AstBaseType)Idx>{}};
    }
}

template <size_t... Idx>
AstNormalizedTypeProperties parseTypePropertiesImpl(
    ErrorContext& errs,
    AstBaseType baseType,
    AstTypeProperties const& props,
    std::index_sequence<Idx...>) {
    AstNormalizedTypeProperties out;
    (parseTypePropertiesSingle<Idx>(errs, baseType, props, out), ...);
    return out;
}

AstNormalizedTypeProperties parseTypeProperties(
    ErrorContext& errs,
    AstBaseType baseType,
    AstTypeProperties const& props) {
    return parseTypePropertiesImpl(
        errs, baseType, props,
        std::make_index_sequence<(size_t)AstBaseType::TOTAL_AST_BASE_TYPES>());
}

}  // namespace ao::schema