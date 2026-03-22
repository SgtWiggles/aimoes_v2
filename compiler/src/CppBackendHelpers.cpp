#include "CppBackendHelpers.h"

GeneratedObject generateAccessorDecl(CppCodeGenContext& ctx,
                                     size_t typeId,
                                     ao::schema::ir::Type const& type) {
    auto name = std::format("CppAccessor_{}", typeId);

    return GeneratedObject{
        .name = {.ns = "aosl_detail", .name = name},
        .decl = std::format("struct CppAccessorFor_{};", name),
        .impl = {},
    };
}
