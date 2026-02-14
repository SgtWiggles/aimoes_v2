#include "AstValidateTypeProperties.h"

#include "ao/utils/Overloaded.h"

namespace ao::schema {
void validateAstTypeProperties(ErrorContext& err, AstMessageBlock& block);
void validateAstTypeProperties(ErrorContext& err, AstType& type);

void validateAstTypeProperties(ErrorContext& err, AstType& type) {
    type.normalizedProperties = NormalizedAstTypeProperties{
        parseTypeProperties(err, type.type, type.properties),
    };

    for (auto const& subtype : type.subtypes) {
        if (subtype)
            validateAstTypeProperties(err, *subtype);
        else
            err.fail({
                .code = ErrorCode::INTERNAL,
                .message = "Got nullptr for type",
                .loc = type.loc,
            });
    }

    validateAstTypeProperties(err, type.block);
}
void validateAstTypeProperties(ErrorContext& err, AstMessageBlock& block) {
    for (auto& field : block.fields) {
        std::visit(Overloaded{
                       [&err](AstField& f) {
                           validateAstTypeProperties(err, f.typeName);
                       },
                       [](AstFieldReserved const&) {},
                       [](AstDefault const&) {},
                   },
                   field.field);
    }
}

void validateAstTypeProperties(ErrorContext& err,
                               SemanticContext::Module& mod) {
    for (auto& decl : mod.ast->decls) {
        std::visit(Overloaded{
                       [&err](AstMessage& msg) {
                           validateAstTypeProperties(err, msg.block);
                       },
                       [](AstImport const&) {},
                       [](AstPackageDecl const&) {},
                       [](AstDefault const&) {},
                   },
                   decl.decl);
    }
}

void validateAstTypeProperties(
    ErrorContext& err,
    std::unordered_map<std::string, SemanticContext::Module>& modules) {
    for (auto& [path, m] : modules) {
        validateAstTypeProperties(err, m);
    }
}
}  // namespace ao::schema