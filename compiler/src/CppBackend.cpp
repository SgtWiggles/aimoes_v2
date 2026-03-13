#include "ao/schema/CppBackend.h"

#include <sstream>

#include "ao/utils/Array.h"
#include "ao/utils/Overloaded.h"

namespace ao::schema::cpp {
struct CppCodeGenContext {
    ir::IR const& ir;
    ErrorContext& errs;

    std::vector<std::string> generatedTypeNames;
    std::vector<std::optional<std::string>> generatedTypeDecls;
    std::vector<std::string> generatedTypeDefs;
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
                        return std::format("int{}_t", v.width);
                    case ir::Scalar::UINT:
                        return std::format("uint{}_t", v.width);
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
            [typeId](ir::Optional const& v) -> std::string { return {}; },
            [typeId](IdFor<ir::OneOf> const& v) -> std::string { return {}; },
            [typeId](IdFor<ir::Message> const& v) -> std::string {
                return std::format("struct Type_{}_Msg;", typeId);
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
                    auto const& arm = ctx.ir.fields[armField.idx];
                    ss << "\n    " << ctx.generatedTypeNames[arm.type.idx];
                    if (needsComma) {
                        ss << ", ";
                    } else {
                        needsComma = true;
                    }
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

bool generateCppCode(ir::IR const& ir, ErrorContext& errs, std::ostream& out) {
    if (!errs.ok())
        return false;

    CppCodeGenContext ctx{ir, errs};
    size_t i;

    i = 0;
    for (auto const& type : ir.types) {
        auto typeName = generateTypeName(ctx, i, type);
        ctx.generatedTypeNames.emplace_back(std::move(typeName));
        ++i;
    }

    i = 0;
    for (auto const& type : ir.types) {
        auto typeDef = generateTypeDecl(ctx, i, type);
        ctx.generatedTypeDefs.emplace_back(std::move(typeDef));
        ++i;
    }
    i = 0;
    for (auto const& type : ir.types) {
        auto typeDecl = generateTypeDecl(ctx, i, type);
        ctx.generatedTypeDecls.emplace_back(std::move(typeDecl));
        ++i;
    }

    // Messages are the most important thing to be generated here
    // TODO generate namespaces and final message names
    for (auto const& msg : ir.messages) {
        // Parse the key, generate the namespace + using decl converting Type to Struct
        // Also generate the reflection table for iteration later
    }

    return errs.ok();
}
}  // namespace ao::schema::cpp
