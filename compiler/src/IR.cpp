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

IdFor<Type> generateIR(IRContext& ctx, AstTypeName const& type) {
    // TODO generate the type of the oneof
    return {};
}
IdFor<Type> generateIR(IRContext& ctx, AstFieldOneOf const& type) {
    // TODO generate the type of the oneof
    return {};
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
            [&](AstFieldOneOf const& v) -> Ret {
                auto field = Field{
                    .name = ctx.strings.getId(v.name),
                    .fieldNumber = v.fieldNumber,
                    .type = generateIR(ctx, v),
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
