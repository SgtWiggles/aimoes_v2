#include "ComputeNormalizedTypeParameters.h"

#include <format>

#include "ao/utils/Overloaded.h"

using namespace ao::schema;

void computeNormalizedTypeParameters(ErrorContext& errs, AstMessageBlock& blk);
void computeNormalizedTypeParameters(ErrorContext& errs, AstType& type);

bool parseLiteral(ErrorContext& ctx,
                  AstValueLiteral const& value,
                  std::optional<int>& output) {
    ctx.require(value.type == ValueLiteralType::INT,
                {
                    ErrorCode::INVALID_VALUE_FOR_TYPE_PROPERTY,
                    "Expected integer for type property",
                    value.loc,
                });
    if (value.type != ValueLiteralType::INT)
        return false;
    output = std::stoi(value.contents);
    return true;
}
bool parseLiteral(ErrorContext& ctx,
                  AstValueLiteral const& value,
                  std::optional<std::string>& output) {
    ctx.require(value.type == ValueLiteralType::INT,
                {
                    ErrorCode::INVALID_VALUE_FOR_TYPE_PROPERTY,
                    "Expected integer for type property",
                    value.loc,
                });
    if (value.type != ValueLiteralType::INT)
        return false;
    output = value.contents;
    return true;
}

template <class T>
std::optional<T> checkProperty(ErrorContext& errs,
                               AstTypeProperty const& property,
                               std::string_view expectedName,
                               std::optional<T>& output) {
    if (expectedName != property.name)
        return {};
    errs.require(
        !output.has_value(),
        {
            ErrorCode::MULTIPLY_DEFINED_TYPE_PROPERTY,
            std::format("Multiply defined type property '{}'", property.name),
            property.loc,
        });
    if (output.has_value())
        return {};

    auto success = parseLiteral(errs, property.value, output);
    if (!success)
        return {};
    return output;
}

void computeNormalizedTypeParameters(ErrorContext& errs, AstType& type) {
    auto toStore = NormalizedAstTypeProperties{};
    for (auto& subtype : type.subtypes) {
        if (!subtype)
            continue;
        computeNormalizedTypeParameters(errs, *subtype);
    }
    computeNormalizedTypeParameters(errs, type.block);

    for (auto const& property : type.properties.props) {
        // TODO add a better means of validating this, these are clunky
        if (auto prop = checkProperty(errs, property, "bits", toStore.bits)) {
            errs.require(*prop > 0,
                         {
                             ErrorCode::INVALID_VALUE_FOR_TYPE_PROPERTY,
                             "bits must have more than 0 bits",
                             property.loc,
                         });
            errs.require(*prop <= 64,
                         {
                             ErrorCode::INVALID_VALUE_FOR_TYPE_PROPERTY,
                             "bits must have at most than 64 bits",
                             property.loc,
                         });
        }

        checkProperty(errs, property, "encoding", toStore.encoding);

        if (auto prop =
                checkProperty(errs, property, "min_len", toStore.minLength)) {
            errs.require(*prop >= 0,
                         {
                             ErrorCode::INVALID_VALUE_FOR_TYPE_PROPERTY,
                             "min_len must be >= 0",
                             property.loc,

                         });
        }
        if (auto prop =
                checkProperty(errs, property, "max_len", toStore.maxLength)) {
            errs.require(*prop >= 0,
                         {
                             ErrorCode::INVALID_VALUE_FOR_TYPE_PROPERTY,
                             "max_len must be >= 0",
                             property.loc,

                         });
        }
    }

    type.normalizedProperties = std::move(toStore);
}

void computeNormalizedTypeParameters(ErrorContext& errs, AstMessageBlock& blk) {
    for (auto& field : blk.fields) {
        auto f = std::get_if<AstField>(&field.field);
        if (!f)
            continue;
        computeNormalizedTypeParameters(errs, f->typeName);
    }
}

void computeNormalizedTypeParameters(
    ErrorContext& errs,
    ao::schema::SemanticContext::Module& module) {
    for (auto const& decl : module.ast->decls) {
        std::visit(ao::Overloaded{
                       [&errs](AstMessage& msg) {
                           computeNormalizedTypeParameters(errs, msg.block);
                       },
                       [](auto const&) {},
                   },
                   decl.decl);
    }
}

void computeNormalizedTypeParameters(
    ErrorContext& errs,
    std::unordered_map<std::string, ao::schema::SemanticContext::Module>&
        modules) {
    for (auto& [path, module] : modules)
        computeNormalizedTypeParameters(errs, module);
}
