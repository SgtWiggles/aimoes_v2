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
    Type currentType = Type{Scalar{Scalar::UINT}};
    if (!type.normalizedProperties) {
        ctx.errors.fail({
            .code = ErrorCode::INTERNAL,
            .message = "Normalized properties was not computed",
            .loc = type.loc,
        });
        return ctx.types.getId(Type{Scalar{Scalar::UINT}});
    }
    switch (type.type) {
        case AstBaseType::BOOL:
            getPropertyFor<AstBaseType::BOOL>(
                ctx.errors, type.normalizedProperties->props, type.loc,
                [&](NormalizedTypeProperty<AstBaseType::BOOL> const& props) {
                    currentType = Type{Scalar{Scalar::BOOL}};
                });
            break;
        case AstBaseType::INT:
            getPropertyFor<AstBaseType::INT>(
                ctx.errors, type.normalizedProperties->props, type.loc,
                [&](NormalizedTypeProperty<AstBaseType::INT> const& props) {
                    currentType = Type{Scalar{
                        .kind = Scalar::INT,
                        .width = props.bits,
                    }};
                });
            break;
        case AstBaseType::UINT:
            getPropertyFor<AstBaseType::UINT>(
                ctx.errors, type.normalizedProperties->props, type.loc,
                [&](NormalizedTypeProperty<AstBaseType::UINT> const& props) {
                    currentType = Type{Scalar{
                        .kind = Scalar::UINT,
                        .width = props.bits,
                    }};
                });
            break;
        case AstBaseType::F32:
            getPropertyFor<AstBaseType::F32>(
                ctx.errors, type.normalizedProperties->props, type.loc,
                [&](NormalizedTypeProperty<AstBaseType::F32> const& props) {
                    currentType = Type{Scalar{
                        .kind = Scalar::F32,
                    }};
                });
            break;
        case AstBaseType::F64:
            getPropertyFor<AstBaseType::F64>(
                ctx.errors, type.normalizedProperties->props, type.loc,
                [&](NormalizedTypeProperty<AstBaseType::F64> const& props) {
                    currentType = Type{Scalar{
                        .kind = Scalar::F64,
                    }};
                });
            break;
        case AstBaseType::STRING:
            getPropertyFor<AstBaseType::STRING>(
                ctx.errors, type.normalizedProperties->props, type.loc,
                [&](NormalizedTypeProperty<AstBaseType::STRING> const& props) {
                    currentType = Type{
                        Array{
                            .type = ctx.types.getId(Type{Scalar{
                                .kind = Scalar::UINT,
                                .width = 8,
                            }}),
                        },
                    };
                });
            break;
        case AstBaseType::BYTES:
            getPropertyFor<AstBaseType::BYTES>(
                ctx.errors, type.normalizedProperties->props, type.loc,
                [&](NormalizedTypeProperty<AstBaseType::BYTES> const& props) {
                    currentType = Type{
                        Array{
                            .type = ctx.types.getId(Type{Scalar{
                                .kind = Scalar::UINT,
                                .width = 8,
                            }}),
                        },
                    };
                });
            break;

        case AstBaseType::ARRAY:
            getPropertyFor<AstBaseType::ARRAY>(
                ctx.errors, type.normalizedProperties->props, type.loc,
                [&](NormalizedTypeProperty<AstBaseType::ARRAY> const& props) {
                    currentType = Type{Array{
                        .type = generateIR(ctx, *type.subtypes[0]),
                    }};
                });
            break;
        case AstBaseType::OPTIONAL:
            getPropertyFor<AstBaseType::OPTIONAL>(
                ctx.errors, type.normalizedProperties->props, type.loc,
                [&](NormalizedTypeProperty<AstBaseType::OPTIONAL> const&
                        props) {
                    currentType = Type{Optional{
                        .type = generateIR(ctx, *type.subtypes[0]),
                    }};
                });
            break;
        case AstBaseType::ONEOF: {
            getPropertyFor<AstBaseType::ONEOF>(
                ctx.errors, type.normalizedProperties->props, type.loc,
                [&](NormalizedTypeProperty<AstBaseType::ONEOF> const& props) {
                    auto oneof = OneOf{};
                    std::vector<std::pair<uint64_t, IdFor<Field>>> arms;
                    for (auto const& [fieldNum, field] :
                         type.block.fieldsByFieldId) {
                        std::visit(
                            Overloaded{
                                [&arms, &ctx, &field](AstField const& f) {
                                    auto fieldForInsert = Field{};
                                    fieldForInsert.name =
                                        ctx.strings.getId(f.name);
                                    fieldForInsert.fieldNumber = f.fieldNumber;
                                    fieldForInsert.type =
                                        generateIR(ctx, f.typeName);
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

                    std::sort(arms.begin(), arms.end(),
                              [](auto const& l, auto const& r) {
                                  return l.first < r.first;
                              });

                    for (auto const& arm : arms) {
                        oneof.arms.push_back(arm.second);
                    }
                    auto id = ctx.oneOfs.getId(oneof);
                    currentType = Type{id};
                });
        } break;
        case AstBaseType::USER:
            if (type.resolvedDef) {
                getPropertyFor<AstBaseType::USER>(
                    ctx.errors, type.normalizedProperties->props, type.loc,
                    [&](NormalizedTypeProperty<AstBaseType::USER> const&
                            props) {
                        currentType = Type{
                            ctx.messages.getId(*type.resolvedDef),
                        };
                    });
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
