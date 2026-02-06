#include "ComputeDirectives.h"

#include <deque>
#include <variant>

#include "ao/utils/Overloaded.h"

using namespace ao;
using namespace ao::schema;

struct DirectiveTable {
    std::unordered_map<std::string,
                       std::unordered_map<std::string, AstValueLiteral>>
        directives;

    void setDirectives(AstDirectiveBlock const& block) {
        for (auto const& dir : block.directives) {
            for (auto const& [tag, value] : dir.properties) {
                directives[dir.directiveName][tag] = value;
            }
        }
    }

    void merge(DirectiveTable const& other) {
        for (auto const& [profile, directives] : other.directives) {
            for (auto const& [tag, value] : directives)
                this->directives[profile][tag] = value;
        }
    }
};

struct DirectiveContext {
    ErrorContext& errors;
    // Deque for pointer stability
    std::deque<DirectiveTable> tables;

    DirectiveTable getCurrentTable() const {
        DirectiveTable ret;
        for (auto& table : tables) {
            ret.merge(table);
        }
        return ret;
    }
    void pop() {
        if (!tables.empty())
            tables.pop_back();
    }
    DirectiveTable& push() {
        tables.push_back({});
        return tables.back();
    }
};
void computeMessageBlockDirectives(DirectiveContext& ctx, AstMessageBlock& blk);

void computeTypeNameDirectives(DirectiveContext& ctx, AstType& type) {
    switch (type.type) {
        case AstBaseType::INT:
        case AstBaseType::UINT:
        case AstBaseType::F32:
        case AstBaseType::F64:
        case AstBaseType::STRING:
        case AstBaseType::BYTES:
        case AstBaseType::USER:
            break;
        case AstBaseType::ARRAY:
        case AstBaseType::OPTIONAL:
            for (auto& sub : type.subtypes) {
                computeTypeNameDirectives(ctx, *sub);
            }
            break;
        case AstBaseType::ONEOF:
            computeMessageBlockDirectives(ctx, type.block);
            break;
    }
}

void computeMessageBlockDirectives(DirectiveContext& ctx,
                                   AstMessageBlock& blk) {
    auto& currentBlock = ctx.push();
    for (auto& field : blk.fields) {
        std::visit(Overloaded{
                       [&](AstField& field) {
                           auto& fieldBlock = ctx.push();
                           fieldBlock.setDirectives(field.directives);
                           field.directives.effectiveDirectives =
                               ctx.getCurrentTable().directives;
                           computeTypeNameDirectives(ctx, field.typeName);
                           ctx.pop();
                       },
                       [&](AstDefault& field) {
                           currentBlock.setDirectives(field.directives);
                       },

                       [](AstFieldReserved) {},
                   },
                   field.field);
    }
    ctx.pop();
}

bool computeModuleDirectives(ao::schema::ErrorContext& errors,
                             ao::schema::SemanticContext::Module& module) {
    DirectiveContext ctx{errors, {}};
    auto& globalTable = ctx.push();
    for (auto& decl : module.ast->decls) {
        std::visit(Overloaded{
                       [](AstImport&) {},
                       [](AstPackageDecl&) {},
                       [&](AstMessage& msg) {
                           auto& localDirectives = ctx.push();
                           localDirectives.setDirectives(msg.directives);
                           msg.directives.effectiveDirectives =
                               ctx.getCurrentTable().directives;
                           ctx.pop();
                           computeMessageBlockDirectives(ctx, msg.block);
                       },
                       [&](AstDefault& node) {
                           globalTable.setDirectives(node.directives);
                       },
                   },
                   decl.decl);
    }
    return errors.errors.size() == 0;
}

bool computeDirectives(
    ao::schema::ErrorContext& errors,
    std::unordered_map<std::string, ao::schema::SemanticContext::Module>&
        modules) {
    for (auto& [path, module] : modules)
        computeModuleDirectives(errors, module);
    return errors.errors.size() == 0;
}
