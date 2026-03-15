#include "ao/schema/CppBackend.h"

#include <sstream>

#include "ao/utils/Array.h"
#include "ao/utils/Overloaded.h"

namespace ao::schema::cpp {
struct CppCodeGenContext {
    ir::IR const& ir;
    ErrorContext& errs;

    std::vector<std::string> generatedTypeNames;
    std::vector<std::string> generatedTypeDecls;
    std::vector<std::string> generatedTypeDefs;
    std::vector<std::string> generatedMessages;
};

uint8_t getCppBitWidth(CppCodeGenContext& ctx, uint64_t width) {
    // default to uint64_t if no width specified, this is
    // consistent with the codec table generation
    if (width == 0)
        return 64;

    static constexpr auto items = makeArray<int>(8, 16, 32, 64);
    for (auto item : items) {
        if (width <= item)
            return static_cast<uint8_t>(item);
    }

    ctx.errs.fail({
        .code = ErrorCode::INTERNAL,
        .message = std::format("Unsupported bit width for C++: {}", width),
        .loc = {},
    });
    return 0;
}

static std::string generateTypeName(CppCodeGenContext& ctx,
                                    size_t typeId,
                                    ir::Type const& type) {
    return std::visit(
        Overloaded{
            [&ctx](ir::Scalar const& v) -> std::string {
                auto cppWidth = getCppBitWidth(ctx, v.width);
                switch (v.kind) {
                    case ir::Scalar::BOOL:
                        return "bool";
                    case ir::Scalar::INT:
                        return std::format("int{}_t", cppWidth);
                    case ir::Scalar::UINT:
                        return std::format("uint{}_t", cppWidth);
                    case ir::Scalar::F32:
                        return "float";
                    case ir::Scalar::F64:
                        return "double";
                    case ir::Scalar::CHAR:
                        return "char";
                    case ir::Scalar::BYTE:
                        return "std::byte";
                    default:
                        ctx.errs.fail({
                            .code = ErrorCode::INTERNAL,
                            .message = std::format(
                                "Unknown C++ scalar type: {}", (int)v.kind),
                            .loc = {},
                        });
                        break;
                }

                return "<error>";
            },
            [typeId, &ctx](ir::Array const& v) -> std::string {
                auto scalar =
                    std::get_if<ir::Scalar>(&ctx.ir.types[v.type.idx].payload);
                if (scalar) {
                    if (scalar->kind == ir::Scalar::CHAR)
                        return "std::string";
                    if (scalar->kind == ir::Scalar::BYTE)
                        return "std::vector<std::byte>";
                }
                return std::format("Type_{}_Arr", typeId);
            },
            [typeId](ir::Optional const& v) -> std::string {
                return std::format("Type_{}_Opt", typeId);
            },
            [typeId](IdFor<ir::OneOf> const& v) -> std::string {
                return std::format("Type_{}_Oneof", typeId);
            },
            [typeId](IdFor<ir::Message> const& v) -> std::string {
                return std::format("Type_{}_Msg", typeId);
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
                return std::format("struct {};",
                                   ctx.generatedTypeNames[typeId]);
            },
        },
        type.payload);
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
                return std::format("using {} = std::vector<{}>",
                                   ctx.generatedTypeNames[typeId],
                                   ctx.generatedTypeNames[v.type.idx]);
            },
            [&ctx,
             typeId](ir::Optional const& v) -> std::optional<std::string> {
                return std::format("using {} = std::optional<{}>",
                                   ctx.generatedTypeNames[typeId],
                                   ctx.generatedTypeNames[v.type.idx]);
            },
            [&ctx,
             typeId](IdFor<ir::OneOf> const& v) -> std::optional<std::string> {
                std::stringstream ss;
                ss << std::format("using {} = std::variant<",
                                  ctx.generatedTypeNames[typeId]);
                bool needsComma = false;
                for (auto const& armField : ctx.ir.oneOfs[v.idx].arms) {
                    if (needsComma) {
                        ss << ", ";
                    } else {
                        needsComma = true;
                    }

                    auto const& arm = ctx.ir.fields[armField.idx];
                    ss << "\n    " << ctx.generatedTypeNames[arm.type.idx];
                }
                ss << "\n>";
                return ss.str();
            },
            [&ctx, typeId](
                IdFor<ir::Message> const& v) -> std::optional<std::string> {
                auto const& msg = ctx.ir.messages[v.idx];
                std::stringstream ss;
                ss << std::format("struct Type_{}_Msg {{\n", typeId);
                for (auto const& fieldId : msg.fields) {
                    auto const& field = ctx.ir.fields[fieldId.idx];
                    auto const& fieldName = ctx.ir.strings[field.name.idx];
                    auto const& fieldTypeName =
                        ctx.generatedTypeNames[field.type.idx];
                    ss << std::format("    {} {};\n", fieldTypeName, fieldName);
                }
                ss << "};";
                return ss.str();
            },
        },
        type.payload);
}

std::vector<std::string_view> parsePackageName(std::string_view str) {
    std::vector<std::string_view> output;
    size_t lastWindow = 0;
    for (size_t idx = 0; idx < str.size(); ++idx) {
        if (str[idx] != '.')
            continue;
        auto count = idx - lastWindow;
        output.emplace_back(str.substr(lastWindow, count));
        lastWindow = idx + 1;
    }
    if (lastWindow < str.size()) {
        auto finalName = str.substr(lastWindow, str.size() - lastWindow);
        output.emplace_back(finalName);
    }
    return output;
}

static std::optional<std::string> generateNamespaceForwarding(
    CppCodeGenContext& ctx,
    size_t typeId,
    IdFor<ir::Message> const& msgIdx) {
    std::stringstream ss;
    auto const& msg = ctx.ir.messages[msgIdx.idx];
    auto& name = ctx.ir.strings[msg.name.idx];
    auto packageName = parsePackageName(name);
    if (packageName.empty())
        return {};

    auto namespaceName = std::string{};
    for (size_t idx = 0; idx < packageName.size() - 1; ++idx) {
        if (!namespaceName.empty())
            namespaceName += "::";
        namespaceName += packageName[idx];
    }

    if (!namespaceName.empty()) {
        ss << "namespace " << namespaceName << " {\n";
    }

    ss << "using " << packageName.back()
       << " = aosl_detail::" << ctx.generatedTypeNames[typeId] << "\n";

    if (!namespaceName.empty()) {
        ss << "}\n";
    }

    return ss.str();
}

static std::optional<std::string> generateTypeAccessors() {



}



void generateCppCode(CppCodeGenContext& ctx, std::ostream& out) {
    out << R"(#include <vector>
#include <variant>
#include <cstdint>

namespace aosl_detail {
)";

    for (auto const& def : ctx.generatedTypeDecls) {
        out << def << "\n";
    }

    for (auto const& def : ctx.generatedTypeDefs) {
        out << def << "\n";
    }

    out << "\n}\n";

    for (auto const& def : ctx.generatedMessages) {
        out << def << "\n";
    }
}



bool generateCppCode(ir::IR const& ir, ErrorContext& errs, std::ostream& out) {
    if (!errs.ok())
        return false;

    CppCodeGenContext ctx{ir, errs};
    enumerate(ir.types, [&ctx](size_t i, auto const& type) {
        auto typeName = generateTypeName(ctx, i, type);
        ctx.generatedTypeNames.emplace_back(std::move(typeName));
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
    enumerate(ir.types, [&ctx](size_t i, auto const& type) {
        auto msg = std::get_if<IdFor<ir::Message>>(&type.payload);
        if (!msg)
            return;
        auto msgForward = generateNamespaceForwarding(ctx, i, *msg);
        if (!msgForward)
            return;
        ctx.generatedMessages.emplace_back(std::move(*msgForward));
    });

    generateCppCode(ctx, out);
    return errs.ok();
}
}  // namespace ao::schema::cpp
