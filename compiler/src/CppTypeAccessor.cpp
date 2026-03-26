#include "CppBackendHelpers.h"

#include <string>
#include <variant>

#include "ao/schema/IR.h"
#include "ao/utils/Overloaded.h"

#include "CppTypeAccessor.h"

using namespace ao;
using namespace ao::schema;

GeneratedObject generateAccessorDecl(CppCodeGenContext& ctx,
                                     size_t typeId,
                                     ao::schema::ir::Type const& type) {
    auto name = std::format("CppAccessorFor_{}", typeId);
    TypeName qname = {.ns = "aosl_detail", .name = name};

    return GeneratedObject{
        .name = qname,
        .fwdDecl = std::format("struct {};", name),
        .decl = replaceMany(TYPE_ACCESSOR_TEMPLATE,
                            {
                                {"@NAMESPACE", qname.ns},
                                {"@NAME", qname.name},
                            }),
        .impl = {},
    };
}

static void generateTypeAccessorImpl(CppCodeGenContext& ctx,
                                     size_t typeId,
                                     ir::Type const& type) {
    std::stringstream ss;
    auto const& typeName = ctx.generatedTypeNames[typeId];
    std::visit(Overloaded{
                   [&](ir::Scalar const& v) {
                       generateTypeAccessorScalar(ctx, typeId, v);
                   },
                   [&](ir::Array const& v) {
                       generateTypeAccessorArray(ctx, typeId, v);
                   },
                   [&](ir::Optional const& v) {
                       generateTypeAccessorOptional(ctx, typeId, v);
                   },
                   [&](IdFor<ir::OneOf> const& v) {
                       generateTypeAccessorOneof(ctx, typeId,
                                                 ctx.ir.oneOfs[v.idx]);
                   },
                   [&](IdFor<ir::Message> const& v) {
                       generateTypeAccessorMessage(ctx, typeId, v);
                   },
                   [&](IdFor<ir::Enum> const& v) {
                       generateTypeAccessorEnum(ctx, typeId, v);
                   },
               },
               type.payload);
}

void generateTypeAccessor(CppCodeGenContext& ctx,
                          size_t typeId,
                          ir::Type const& type) {
    generateTypeAccessorImpl(ctx, typeId, type);
}
