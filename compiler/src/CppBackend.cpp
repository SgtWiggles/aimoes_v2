#include "ao/schema/CppBackend.h"

#include "ao/schema/CppAdapter.h"

#include <fstream>
#include <sstream>

#include "ao/utils/Array.h"
#include "ao/utils/Overloaded.h"

#include "ao/pack/OStreamWriteStream.h"

#include "CppBackendHelpers.h"
#include "CppTypeAccessor.h"

namespace ao::schema::cpp {

static TypeName generateTypeName(CppCodeGenContext& ctx,
                                 size_t typeId,
                                 ir::Type const& type) {
    return std::visit(
        Overloaded{
            [&ctx](ir::Scalar const& v) -> TypeName {
                auto cppWidth = getCppBitWidth(ctx, v.width);
                switch (v.kind) {
                    case ir::Scalar::BOOL:
                        return {{}, "bool"};
                    case ir::Scalar::INT:
                        return {{}, std::format("int{}_t", cppWidth)};
                    case ir::Scalar::UINT:
                        return {{}, std::format("uint{}_t", cppWidth)};
                    case ir::Scalar::F32:
                        return {{}, "float"};
                    case ir::Scalar::F64:
                        return {{}, "double"};
                    case ir::Scalar::CHAR:
                        return {{}, "char"};
                    case ir::Scalar::BYTE:
                        return {{}, "std::byte"};
                    default:
                        ctx.errs.fail({
                            .code = ErrorCode::INTERNAL,
                            .message = std::format(
                                "Unknown C++ scalar type: {}", (int)v.kind),
                            .loc = {},
                        });
                        break;
                }

                return {{}, "<error>"};
            },
            [typeId, &ctx](ir::Array const& v) -> TypeName {
                auto scalar =
                    std::get_if<ir::Scalar>(&ctx.ir.types[v.type.idx].payload);
                if (scalar) {
                    if (scalar->kind == ir::Scalar::CHAR)
                        return {{}, "std::string"};
                    if (scalar->kind == ir::Scalar::BYTE)
                        return {{}, "std::vector<std::byte>"};
                }
                return {"aosl_detail", std::format("Type_{}_Arr", typeId)};
            },
            [typeId](ir::Optional const& v) -> TypeName {
                return {"aosl_detail", std::format("Type_{}_Opt", typeId)};
            },
            [typeId](IdFor<ir::OneOf> const& v) -> TypeName {
                return {"aosl_detail", std::format("Type_{}_Oneof", typeId)};
            },
            [&ctx, typeId](IdFor<ir::Message> const& v) -> TypeName {
                auto const& fqn =
                    ctx.ir.strings[ctx.ir.messages[v.idx].name.idx];
                auto messageName = getMessageName(fqn);
                auto namespaceName = getNamespaceName(fqn);
                if (!messageName)
                    ctx.errs.fail({
                        .code = ao::schema::ErrorCode::INTERNAL,
                        .message = std::format(
                            "Got message '{}' with namespace '{}' with empty "
                            "name!",
                            fqn, namespaceName.value_or(std::string{""})),
                        .loc = {},
                    });
                return {namespaceName.value_or(std::string{}),
                        std::string{messageName.value_or(std::string_view{})}};
            },
        },
        type.payload);
}

static std::optional<std::string> generateTypeDecl(CppCodeGenContext& ctx,
                                                   size_t typeId,
                                                   ir::Type const& type) {
    return std::visit(
        Overloaded{
            [&ctx](ir::Scalar const& v) -> std::optional<std::string> {
                return {};
            },
            [typeId, &ctx](ir::Array const& v) -> std::optional<std::string> {
                return {};
            },
            [typeId](ir::Optional const& v) -> std::optional<std::string> {
                return {};
            },
            [typeId](IdFor<ir::OneOf> const& v) -> std::optional<std::string> {
                return {};
            },
            [&ctx, typeId](
                IdFor<ir::Message> const& v) -> std::optional<std::string> {
                auto const& fqn =
                    ctx.ir.strings[ctx.ir.messages[v.idx].name.idx];
                auto messageName = getMessageName(fqn);
                auto namespaceName = getNamespaceName(fqn);

                return replaceMany(
                    R"(namespace @NAMESPACE {
    struct @MSG;
}
)",
                    {
                        {"@NAMESPACE", *namespaceName},
                        {"@MSG", *messageName},
                    });
            },
        },
        type.payload);
}

static void generateMessageDirectives(CppCodeGenContext& ctx,
                                      std::stringstream& ss,
                                      size_t typeId,
                                      IdFor<ir::Message> const& msgIdx) {
    auto const& msg = ctx.ir.messages[msgIdx.idx];
    auto const& directiveSet = ctx.ir.directiveSets[msg.directives.idx];
    ir::DirectiveProfile const* directiveProfile = nullptr;
    for (auto const& profileIdx : directiveSet.directives) {
        auto const& profile = ctx.ir.directiveProfiles[profileIdx.idx];
        if (profile.domain != ir::DirectiveProfile::Cpp)
            continue;
        directiveProfile = &profile;
        break;
    }
    if (!directiveProfile)
        return;

    for (auto const& propertyIdx : directiveProfile->properties) {
        auto const& property = ctx.ir.directiveProperties[propertyIdx.idx];
        auto const& name = ctx.ir.strings[property.name.idx];
        if (name == "compare") {
            auto value = std::get_if<IdFor<std::string>>(&property.value.value);
            if (!value)
                continue;
            auto const& compareMode = ctx.ir.strings[value->idx];
            auto const& selfTypeName = ctx.generatedTypeNames[typeId].name;
            if (compareMode == "none") {
                continue;
            } else if (compareMode == "default") {
                ss << std::format(
                    "friend auto "
                    "operator<=>({0} "
                    "const&, {0} const&) "
                    " = "
                    "default",
                    selfTypeName);
            } else if (compareMode == "define") {
                ss << std::format(
                    "friend auto "
                    "operator<=>({0} "
                    "const&, {0} const&) ",
                    selfTypeName);
            } else {
                continue;
            }
            ss << ";\n";
        } else if (name == "define") {
            auto value = std::get_if<IdFor<std::string>>(&property.value.value);
            if (!value)
                continue;
            auto const& definition = ctx.ir.strings[value->idx];
            ss << definition << ";\n";
        }
    }
}

static std::optional<std::string> generateTypeDef(CppCodeGenContext& ctx,
                                                  size_t typeId,
                                                  ir::Type const& type) {
    return std::visit(
        Overloaded{
            [&ctx](ir::Scalar const& v) -> std::optional<std::string> {
                return {};
            },
            [&ctx, typeId](ir::Array const& v) -> std::optional<std::string> {
                auto scalar =
                    std::get_if<ir::Scalar>(&ctx.ir.types[v.type.idx].payload);
                if (scalar) {
                    if (scalar->kind == ir::Scalar::CHAR)
                        return {};
                    if (scalar->kind == ir::Scalar::BYTE)
                        return {};
                }
                return std::format(
                    "namespace aosl_detail {{ using {} = std::vector<{}>; }}",
                    ctx.generatedTypeNames[typeId].name,
                    ctx.generatedTypeNames[v.type.idx].qualifiedName());
            },
            [&ctx,
             typeId](ir::Optional const& v) -> std::optional<std::string> {
                return std::format(
                    "namespace aosl_detail {{ using {} = std::optional<{}>; }}",
                    ctx.generatedTypeNames[typeId].name,
                    ctx.generatedTypeNames[v.type.idx].qualifiedName());
            },
            [&ctx,
             typeId](IdFor<ir::OneOf> const& v) -> std::optional<std::string> {
                std::stringstream ss;
                ss << std::format(
                    "namespace aosl_detail {{ using {} = "
                    "std::variant<std::monostate",
                    ctx.generatedTypeNames[typeId].name);
                for (auto const& armField : ctx.ir.oneOfs[v.idx].arms) {
                    ss << ", ";
                    auto const& arm = ctx.ir.fields[armField.idx];
                    ss << "\n "
                       << ctx.generatedTypeNames[arm.type.idx].qualifiedName();
                }
                ss << "\n>; }\n";
                return ss.str();
            },
            [&ctx, typeId](
                IdFor<ir::Message> const& v) -> std::optional<std::string> {
                auto const& msg = ctx.ir.messages[v.idx];
                auto const& typeName = ctx.generatedTypeNames[typeId];
                std::stringstream ss;
                if (!typeName.ns.empty())
                    ss << "namespace " << typeName.ns << "{";

                ss << std::format("struct {} {{\n", typeName.name);
                for (auto const& fieldId : msg.fields) {
                    auto const& field = ctx.ir.fields[fieldId.idx];
                    auto const& fieldName = ctx.ir.strings[field.name.idx];
                    auto const& fieldTypeName =
                        ctx.generatedTypeNames[field.type.idx];
                    ss << std::format(" {} {};\n",
                                      fieldTypeName.qualifiedName(), fieldName);
                }
                ss << std::format(
                    "using Accessor = {};\n",
                    ctx.generatedAccessors[typeId].name.qualifiedName());
                ss << std::format(
                    "static constexpr uint32_t AOSL_MESSAGE_ID = {};\n", v.idx);
                ss << std::format(
                    "static constexpr uint32_t AOSL_TYPE_ID = {};\n", typeId);

                generateMessageDirectives(ctx, ss, typeId, v);

                ss << "};";

                if (!typeName.ns.empty())
                    ss << "}";
                return ss.str();
            },
        },
        type.payload);
}

void generateHeaders(CppCodeGenContext& ctx, std::ostream& out) {
    out << R"(
#pragma once
#include <vector>
#include <variant>
#include <cstdint>

#include <ao/schema/CppAdapter.h>
#include <ao/schema/IR.h>

)";

    out << replaceMany(R"(
constexpr inline ao::schema::ir::IRHeader getCompiledHeader() {
 return ao::schema::ir::IRHeader{
 .magic = @MAGIC,
 .version = @VERSION,
 };
};
static_assert(getCompiledHeader() == ao::schema::ir::IRHeader{}, "Generated code was not the same as current code, regenerate the schema");
)",
                       {
                           {"@MAGIC", std::to_string(ir::IRHeader{}.magic)},
                           {"@VERSION", std::to_string(ir::IRHeader{}.version)},
                       });

    out << "namespace aosl_detail {\n";
    for (auto const& type : ctx.generatedAccessors) {
        out << type.fwdDecl << "\n";
    }
    out << "\n}\n";

    for (auto const& def : ctx.generatedTypeDecls) {
        out << def << "\n";
    }

    for (auto const& def : ctx.generatedTypeDefs) {
        out << def << "\n";
    }

    out << "namespace aosl_detail {\n";
    for (auto const& type : ctx.generatedAccessors) {
        out << type.decl << "\n";
    }
    out << "\n}\n";
}

void generateCpp(CppCodeGenContext& ctx,
                 std::ostream& out,
                 std::string headerPath) {
    out << std::format(R"(
#include "{}"

#include <vector>
#include <variant>
#include <cstdint>

#include <ao/schema/CppAdapter.h>
#include <ao/schema/IR.h>

)",
                       headerPath);

    out << "namespace aosl_detail {\n";
    for (auto const& type : ctx.generatedAccessors) {
        out << type.impl << "\n";
    };
    out << "\n}\n";
}

bool generateCppCode(ir::IR const& ir, ErrorContext& errs, OutputFiles& files) {
    if (!errs.ok())
        return false;

    CppCodeGenContext ctx{ir, errs};
    enumerate(ir.types, [&ctx](size_t i, auto const& type) {
        auto typeName = generateTypeName(ctx, i, type);
        ctx.generatedTypeNames.emplace_back(std::move(typeName));
    });
    enumerate(ir.types, [&ctx](size_t i, auto const& type) {
        auto typeName = generateAccessorDecl(ctx, i, type);
        ctx.generatedAccessors.emplace_back(std::move(typeName));
    });
    enumerate(ir.types, [&ctx](size_t i, auto const& type) {
        auto& accessor = ctx.generatedAccessors[i];
        generateTypeAccessor(ctx, i, type);
    });
    enumerate(ir.types, [&ctx](size_t i, auto const& type) {
        auto typeDef = generateTypeDef(ctx, i, type);
        if (!typeDef)
            return;
        ctx.generatedTypeDefs.emplace_back(std::move(*typeDef));
    });
    enumerate(ir.types, [&ctx](size_t i, auto const& type) {
        auto typeDecl = generateTypeDecl(ctx, i, type);
        if (!typeDecl)
            return;
        ctx.generatedTypeDecls.emplace_back(std::move(*typeDecl));
    });

    auto makePath = [&](std::string_view ext) {
        return files.root / files.outDir /
               (files.projectName + std::string{ext});
    };

    auto headerPath = makePath(".h");
    auto cppPath = makePath(".cpp");
    auto irPath = makePath(".aoir");
    auto irHeaderPath = makePath(".aoir.h");

    auto headerStream = files.loader(headerPath, std::ios_base::out, errs);
    auto cppStream = files.loader(cppPath, std::ios_base::out, errs);
    auto irStream =
        files.loader(irPath, std::ios_base::out | std::ios_base::binary, errs);
    auto irHeaderStream = files.loader(irHeaderPath, std::ios_base::out, errs);


    if (!errs.ok())
        return false;
    if (!headerStream || !cppStream || !irStream || !irHeaderStream) {
        errs.fail({
            .code = schema::ErrorCode::INTERNAL,
            .message = "File loader returned null stream",
            .loc = {},
        });
        return false;
    }

    generateHeaders(ctx, *headerStream);
    generateCpp(ctx, *cppStream, (files.projectName + ".h"));

    ao::pack::byte::OStreamWriteStream irWs(*irStream);
    ir::serializeIRFile(irWs, ir);

    std::vector<std::byte> bytes;
    bytes.resize(irWs.byteSize());
    ao::pack::byte::WriteStream ws{std::span{bytes.data(), bytes.size()}};
    ir::serializeIRFile(ws, ir);
    (*irHeaderStream) << "{";
    size_t i = 0;
    for (auto const& b : bytes) {
        if (i % 256 == 0)
            (*irHeaderStream) << "\n";
        (*irHeaderStream) << std::format("'\\x{:02x}', ",
                                         static_cast<uint8_t>(b));
    }
    (*irHeaderStream) << "}";

    return errs.ok();
}
}  // namespace ao::schema::cpp
