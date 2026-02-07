#include "ao/schema/IR.h"

#include "ao/schema/ResourceCache.h"
#include "ao/utils/Overloaded.h"

#include <algorithm>

namespace ao::schema::ir {

struct IRContext {
    ErrorContext errors = {};
    ResourceCache<std::string> strings = {};
    ResourceCache<DirectiveProperty> directiveProperties = {};
    ResourceCache<DirectiveProfile> directiveProfiles = {};
    ResourceCache<DirectiveSet> directiveSets = {};
    ResourceCache<OneOf> oneOfs = {};
    ResourceCache<Field> fields = {};

    // Keyed by symbol ID
    KeyedResourceCache<uint64_t, Message> messages = {};
    ResourceCache<Type> types = {};
};
IdFor<DirectiveSet> generateIR(IRContext& ctx,
                               AstDirectiveBlock const& directives);

IdFor<Type> generateIR(IRContext& ctx, AstType const& type) {
    Type currentType;
    switch (type.type) {
        case AstBaseType::BOOL:
            currentType = Type{Scalar{Scalar::BOOL}};
            break;
        case AstBaseType::INT:
            currentType = Type{Scalar{
                Scalar::INT,
                (uint64_t)type.normalizedProperties->bits.value_or(0),
            }};
            break;
        case AstBaseType::UINT:
            currentType = Type{Scalar{
                Scalar::UINT,
                (uint64_t)type.normalizedProperties->bits.value_or(0),
            }};
            break;
        case AstBaseType::F32:
            currentType = Type{Scalar{Scalar::F32}};
            break;
        case AstBaseType::F64:
            currentType = Type{Scalar{Scalar::F64}};
            break;
        case AstBaseType::STRING:
        case AstBaseType::BYTES:
            currentType = Type{Array{
                .type = ctx.types.getId(Type{Scalar{
                    .kind = Scalar::INT,
                    .width = 8,
                }}),
                .minSize = type.normalizedProperties->minLength,
                .maxSize = type.normalizedProperties->maxLength,
            }};
            break;

        case AstBaseType::ARRAY:
            currentType = Type{Array{
                .type = generateIR(ctx, *type.subtypes[0]),
                .minSize = type.normalizedProperties->minLength,
                .maxSize = type.normalizedProperties->maxLength,
            }};
            break;
        case AstBaseType::OPTIONAL:
            currentType = Type{Optional{
                .type = generateIR(ctx, *type.subtypes[0]),
            }};
            break;
        case AstBaseType::ONEOF: {
            // TODO generate the type of the oneof
            auto oneof = OneOf{};
            std::vector<std::pair<uint64_t, IdFor<Field>>> arms;
            for (auto const& [fieldNum, field] : type.block.fieldsByFieldId) {
                std::visit(
                    Overloaded{
                        [&arms, &ctx, &field](AstField const& f) {
                            auto fieldForInsert = Field{};
                            fieldForInsert.name = ctx.strings.getId(f.name);
                            fieldForInsert.fieldNumber = f.fieldNumber;
                            fieldForInsert.type = generateIR(ctx, f.typeName);
                            fieldForInsert.directives =
                                generateIR(ctx, f.directives);

                            arms.push_back({
                                f.fieldNumber,
                                ctx.fields.getId(fieldForInsert),
                            });
                        },
                        [](AstFieldReserved const&) {},
                        [](AstDefault const&) {},

                    },
                    field->field);
            }

            std::sort(
                arms.begin(), arms.end(),
                [](auto const& l, auto const& r) { return l.first < r.first; });

            for (auto const& arm : arms) {
                oneof.arms.push_back(arm.second);
            }
            auto id = ctx.oneOfs.getId(oneof);
            currentType = Type{id};
        } break;
        case AstBaseType::USER:
            if (type.resolvedDef) {
                currentType = Type{ctx.messages.getId(*type.resolvedDef)};
            } else {
                ctx.errors.require(false,
                                   {
                                       ErrorCode::INTERNAL,
                                       "Unresolved user type found in codegen",
                                       type.loc,
                                   });
            }
            break;
    }
    return ctx.types.getId(currentType);
}

IdFor<DirectiveSet> generateIR(IRContext& ctx,
                               AstDirectiveBlock const& directives) {
    std::vector<DirectiveProfile> profiles;
    static std::unordered_map<std::string, DirectiveProfile::ProfileKind> const
        profileLookup = {
            {"disk", DirectiveProfile::Disk},
            {"net", DirectiveProfile::Net},
            {"cpp", DirectiveProfile::Cpp},
            {"lua", DirectiveProfile::Lua},
            {"field", DirectiveProfile::Field},
            {"message", DirectiveProfile::Message},
        };

    for (auto const& [profileName, profileTags] :
         directives.effectiveDirectives) {
        auto profileIter = profileLookup.find(profileName);

        DirectiveProfile profile;
        if (profileIter == profileLookup.end()) {
            profile.domain = DirectiveProfile::Custom;
            profile.profileName = ctx.strings.getId(profileName);
        } else {
            profile.domain = profileIter->second;
        }

        std::vector<DirectiveProperty> properties;
        for (auto const& [propertyName, propertyValue] : profileTags) {
            DirectiveProperty prop{
                .name = ctx.strings.getId(propertyName),
            };
            switch (propertyValue.type) {
                case ValueLiteralType ::BOOLEAN:
                    prop.value.value = propertyValue.contents == "true";
                    break;
                case ValueLiteralType::INT:
                    prop.value.value = std::stoll(propertyValue.contents);
                    break;
                case ValueLiteralType::NUMBER:
                    prop.value.value = std::stod(propertyValue.contents);
                    break;
                case ValueLiteralType::STRING:
                    prop.value.value =
                        ctx.strings.getId(propertyValue.contents);
                    break;
                default:
                    ctx.errors.require(
                        false,
                        {
                            .code = ErrorCode::INTERNAL,
                            .message = std::format("unknown literal type: {}",
                                                   (int)propertyValue.type),
                        });
                    continue;
            }
            properties.push_back(std::move(prop));
        }
        std::sort(properties.begin(), properties.end());

        for (auto const& prop : properties)
            profile.properties.push_back(ctx.directiveProperties.getId(prop));
        profiles.push_back(std::move(profile));
    }

    std::sort(profiles.begin(), profiles.end());

    DirectiveSet set = {};
    for (auto const& profile : profiles)
        set.directives.push_back(ctx.directiveProfiles.getId(profile));
    return ctx.directiveSets.getId(set);
}

std::optional<IdFor<Field>> generateIR(IRContext& ctx,
                                       AstFieldDecl const& field) {
    using Ret = std::optional<IdFor<Field>>;
    return std::visit(
        Overloaded{
            [&](AstField const& v) -> Ret {
                auto field = Field{
                    .name = ctx.strings.getId(v.name),
                    .fieldNumber = v.fieldNumber,
                    .type = generateIR(ctx, v.typeName),
                    .directives = generateIR(ctx, v.directives),
                };
                return ctx.fields.getId(field);
            },
            [](AstFieldReserved const&) -> Ret { return std::nullopt; },
            [](AstDefault const&) -> Ret { return std::nullopt; },
        },
        field.field);
}

IdFor<Message> generateIR(IRContext& ctx,
                          AstMessage const& decl,
                          SymbolInfo const& info) {
    auto msg = Message{
        .name = ctx.strings.getId(info.qualifiedName),
        .symbolId = info.id,
        .messageNumber = decl.messageId,
        .fields = {},
        .directives = generateIR(ctx, decl.directives),
    };

    for (auto const& field : decl.block.fields) {
        auto fieldId = generateIR(ctx, field);
        if (!fieldId)
            continue;
        msg.fields.push_back(*fieldId);
    }

    return ctx.messages.getId(info.id, msg);
}

void generateIR(IRContext& ctx,
                ao::schema::SemanticContext::Module const& module) {
    for (auto const& [symbolId, message] : module.messagesBySymbolId) {
        ctx.errors.require(message != nullptr,
                           {
                               .code = ErrorCode::INTERNAL,
                               .message = "invalid message pointer!",
                               .loc = {},
                           });
        if (!message)
            continue;
        generateIR(ctx, *message, module.symbolInfoBySymbolId.at(symbolId));
    }
}

IR generateIR(
    std::unordered_map<std::string, ao::schema::SemanticContext::Module> const&
        modules) {
    IRContext ctx{};
    for (auto const& [path, module] : modules) {
        generateIR(ctx, module);
    }

    return IR{
        .strings = ctx.strings.values(),
        .directiveProperties = ctx.directiveProperties.values(),
        .directiveProfiles = ctx.directiveProfiles.values(),
        .directiveSets = ctx.directiveSets.values(),
        .oneOfs = ctx.oneOfs.values(),
        .fields = ctx.fields.values(),
        .messages = ctx.messages.values(),
        .types = ctx.types.values(),
    };
}
}  // namespace ao::schema::ir
