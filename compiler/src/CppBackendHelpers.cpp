#include "CppBackendHelpers.h"

GeneratedObject generateAccessorDecl(CppCodeGenContext& ctx,
                                     size_t typeId,
                                     ao::schema::ir::Type const& type) {
    auto name = std::format("CppAccessorFor_{}", typeId);
    return GeneratedObject{
        .name = {.ns = "aosl_detail", .name = name},
        .fwdDecl = std::format("struct {};", name),
        .decl = {},
        .impl = {},
    };
}
