#include "ao/schema/SemanticContext.h"
#include "ao/schema/Error.h"

#include <format>
#include <queue>
#include <sstream>
#include <unordered_set>
#include <variant>

#include "ao/utils/Overloaded.h"

#include "AstValidateIds.h"
#include "AstValidateTypeProperties.h"
#include "ComputeDirectives.h"

namespace ao::schema {
std::expected<SymbolInfo, Error> SymbolTable::populateFromQualifiedId(
    std::string const& name,
    SourceLocation loc,
    AstDecl const* decl) {
    auto iter = fullyQualifiedNameToId.find(name);
    if (iter != fullyQualifiedNameToId.end()) {
        return std::unexpected(Error{
            ErrorCode::MULTIPLY_DEFINED_SYMBOL,
            std::format("Symbol {} was already defined at location {}", name,
                        loc),
            loc,
        });
    }

    auto id = nextQualifiedIdName++;
    auto info = SymbolInfo{name, id, loc, decl};
    fullyQualifiedNameToId[name] = info;
    return info;
}
std::optional<uint64_t> SymbolTable::getQualifiedId(std::string const& name) {
    auto iter = fullyQualifiedNameToId.find(name);
    if (iter == fullyQualifiedNameToId.end())
        return {};
    return {iter->second.id};
}

struct ModuleLoadContext {
    std::string resolvedPath;
    AstImport import;
};

bool SemanticContext::loadFile(std::string rootPath) {
    std::queue<std::tuple<std::string, SourceLocation>> toResolve;
    auto rootPathResolved = m_frontend->resolvePath("", rootPath);
    if (!rootPathResolved.has_value()) {
        m_errors.errors.push_back({
            ErrorCode::FAILED_TO_RESOLVE_IMPORT,
            rootPathResolved.error(),
            {"internal", 0, 0},
        });
        // Immediately break out, root is invalid
        return false;
    }

    toResolve.push({
        rootPathResolved.value(),
        SourceLocation{
            .file = "",
            .line = 0,
            .col = 0,
        },
    });

    std::unordered_map<std::string, std::shared_ptr<AstFile>> pendingFiles;
    std::unordered_map<std::string, std::unordered_map<std::string, AstImport>>
        dependedOnBy;
    std::unordered_map<std::string, std::unordered_set<std::string>> dependsOn;

    // Computes the inverted dependency graph
    while (!toResolve.empty()) {
        auto currentResolve = toResolve.front();
        auto current = std::get<0>(currentResolve);
        toResolve.pop();

        // Module is already fully resolved
        if (pendingFiles.find(current) != pendingFiles.end())
            continue;
        // This dependency is already resolved
        if (m_modules.find(current) != m_modules.end())
            continue;

        auto moduleAst = m_frontend->loadFile(current);
        if (!moduleAst.has_value()) {
            m_errors.errors.push_back({
                ErrorCode::SYNTAX_ERROR,
                moduleAst.error(),
                std::get<1>(currentResolve),
            });
            continue;
        }
        pendingFiles[current] = moduleAst.value();

        // Insert this node if it's not already here
        if (dependedOnBy.find(current) == dependedOnBy.end())
            dependedOnBy[current] = {};

        for (auto const& decl : moduleAst.value()->decls) {
            auto importDecl = std::get_if<AstImport>(&decl.decl);
            if (!importDecl)
                continue;

            auto dependency =
                m_frontend->resolvePath(current, importDecl->path);
            if (!dependency.has_value()) {
                m_errors.errors.push_back({
                    ErrorCode::FAILED_TO_RESOLVE_IMPORT,
                    dependency.error(),
                    importDecl->loc,
                });
                continue;
            }

            dependsOn[current].insert(dependency.value());
            toResolve.push({dependency.value(), importDecl->loc});
            dependedOnBy[dependency.value()][current] = *importDecl;
        }
    }

    auto getNextFreeNode = [&]() {
        auto currentNode = std::optional<std::string>();
        for (auto const& [moduleName, dependedBy] : dependedOnBy) {
            if (!dependedBy.empty())
                continue;
            currentNode = moduleName;
            return currentNode;
        }
        return currentNode;
    };

    while (auto currentNode = getNextFreeNode()) {
        // No free nodes, figure out why
        if (!currentNode)
            break;
        dependedOnBy.erase(*currentNode);
        for (auto& [baseNode, remainingDependencies] : dependedOnBy)
            remainingDependencies.erase(*currentNode);
    }

    if (!dependedOnBy.empty()) {
        std::stringstream ss;
        ss << "Found cyclical dependencies on imports:";
        for (auto const& [dependedOn, from] : dependedOnBy) {
            for (auto const& [fromPath, fromImport] : from) {
                ss << "\n\t";
                ss << std::format("at {} for file {}", fromImport.loc,
                                  fromImport.path);
            }
        }
        m_errors.errors.push_back({
            ErrorCode::CYCLICAL_IMPORT,
            ss.str(),
            {},
        });
        return false;
    }

    for (auto const& [path, module] : pendingFiles) {
        auto& currentModule = m_modules[path];
        currentModule.resolvedPath = path;
        currentModule.ast = module;
        currentModule.dependencies = dependsOn[path];
    }

    return true;
}

bool setPackageName(ErrorContext& errors,
                    SymbolTable& symbolTable,
                    SemanticContext::Module& module) {
    bool moduleNameSet = false;
    SourceLocation loc;
    for (auto const& decl : module.ast->decls) {
        std::visit(Overloaded{
                       [&](AstPackageDecl const& decl) {
                           // Set name of package
                           if (moduleNameSet) {
                               errors.errors.push_back({
                                   ErrorCode::MULTIPLE_PACKAGE_DECLARATION,
                                   std::format("Package name was previously "
                                               "declared at {}",
                                               loc),
                                   decl.loc,
                               });
                               return;
                           }

                           moduleNameSet = true;
                           module.packageName = decl.name;
                           loc = decl.loc;
                       },
                       [](AstMessage const& message) {
                           // ignore
                       },
                       [](AstImport const&) {
                           // ignore
                       },
                       [](AstDefault const&) {
                           // ignore
                       },
                   },
                   decl.decl);
    }

    if (!moduleNameSet) {
        errors.errors.push_back({
            ErrorCode::MISSING_PACKAGE_DECLARATION,
            "Missing package declaration",
            {module.resolvedPath, 1, 1},
        });
        return false;
    }

    return true;
}

void exportSymbols(ErrorContext& errors,
                   SymbolTable& symbolTable,
                   SemanticContext::Module& module) {
    if (!setPackageName(errors, symbolTable, module))
        return;

    for (auto const& decl : module.ast->decls) {
        std::visit(
            Overloaded{
                [&](AstMessage const& message) {
                    // Add fully resolved name to local definitions
                    auto qualifiedName =
                        module.packageName.qualifyName(message.name);
                    auto entry = symbolTable.populateFromQualifiedId(
                        qualifiedName, message.loc, &decl);
                    if (!entry.has_value()) {
                        errors.errors.push_back(entry.error());
                    } else {
                        module.exportedSymbols[message.name] = entry.value();
                        module.messagesBySymbolId[entry->id] = &message;
                        module.symbolInfoBySymbolId[entry->id] = entry.value();
                    }
                },
                [](AstImport const&) {
                    // ignore
                },
                [](AstDefault const&) {
                    // ignore
                },
                [](AstPackageDecl const& decl) {
                    // ignore
                },
            },
            decl.decl);
    }
}

struct ResolveSymbolsContext {
    std::unordered_map<std::string, std::vector<SymbolInfo>> symbols;
};

void resolveMessage(ErrorContext& errors,
                    ResolveSymbolsContext& ctx,
                    AstMessageBlock& msg);
void resolveTypeName(ErrorContext& errors,
                     ResolveSymbolsContext& ctx,
                     AstType& msg) {
    int const arity = msg.subtypes.size();

    switch (msg.type) {
        case AstBaseType::BOOL:
        case AstBaseType::INT:
        case AstBaseType::UINT:
        case AstBaseType::F32:
        case AstBaseType::F64:
        case AstBaseType::STRING:
        case AstBaseType::BYTES:
        case AstBaseType::USER:
            errors.require(
                arity == 0,
                {
                    ErrorCode::INVALID_TYPE_ARGS,
                    std::format(
                        "Expected no type arguments for type '{}' but got {}",
                        msg.name.toString(), arity),
                    msg.loc,
                });
            break;

        case AstBaseType::ARRAY:
        case AstBaseType::OPTIONAL:
            errors.require(arity == 1,
                           {
                               ErrorCode::INVALID_TYPE_ARGS,
                               std::format("Expected 1 type argument for type "
                                           "constructor '{}' but got {}",
                                           msg.name.toString(), arity),
                               msg.loc,
                           });
            break;
        case AstBaseType::ONEOF:
            errors.require(
                arity == 0,
                {
                    ErrorCode::INVALID_TYPE_ARGS,
                    std::format(
                        "Expected no type arguments for type '{}' but got {}",
                        msg.name.toString(), arity),
                    msg.loc,
                });
            resolveMessage(errors, ctx, msg.block);
            break;

        default:
            errors.require(false,
                           {ErrorCode::INTERNAL, "Unknown base type", msg.loc});
            break;
    }

    for (auto const& type : msg.subtypes) {
        if (!type) {
            errors.require(false, {
                                      ErrorCode::INTERNAL,
                                      "Invalid AST, got nullptr for subtype",
                                      msg.loc,
                                  });
            continue;
        }

        resolveTypeName(errors, ctx, *type);
    }

    // Resolve the base type now
    if (msg.type != AstBaseType::USER)
        return;
    if (msg.name.name.size() == 0) {
        errors.require(false, {
                                  ErrorCode::INTERNAL,
                                  "Invalid qualified name, got empty name",
                                  msg.loc,
                              });
        return;
    }

    auto iter = ctx.symbols.find(msg.name.toString());
    if (iter == ctx.symbols.end() || iter->second.size() == 0) {
        errors.require(false, {
                                  ErrorCode::SYMBOL_NOT_DEFINED,
                                  std::format("Use of undefined type name '{}'",
                                              msg.name.toString()),
                                  msg.loc,
                              });
        return;
    }

    if (iter->second.size() > 1) {
        std::stringstream ss;
        ss << std::format("Ambiguous type name '{}' with candidates: ",
                          msg.name.toString());
        for (auto const& symbol : iter->second) {
            ss << "\n\t" << symbol.qualifiedName << " defined at "
               << std::format("{}", symbol.defLoc);
        }

        errors.require(false, {
                                  ErrorCode::SYMBOL_AMBIGUOUS,
                                  ss.str(),
                                  msg.loc,
                              });
    }

    // Fully qualify the name here
    msg.resolvedFqn = iter->second[0].qualifiedName;
    msg.resolvedDef = iter->second[0].id;
}

void resolveMessage(ErrorContext& errors,
                    ResolveSymbolsContext& ctx,
                    AstMessageBlock& msg) {
    for (auto& f : msg.fields) {
        std::visit(Overloaded{
                       [&](AstField& field) {
                           resolveTypeName(errors, ctx, field.typeName);
                       },
                       [](AstFieldReserved const&) {},
                       [](AstDefault const&) {},
                   },
                   f.field);
    }
}

void resolveModuleSymbols(
    ErrorContext& errors,
    SemanticContext::Module& currentModule,
    std::unordered_map<std::string, SemanticContext::Module> const&
        allModules) {
    ResolveSymbolsContext ctx;

    // Add total list of symbols into the symbol table
    for (auto const& [unqualifiedName, symbolInfo] :
         currentModule.exportedSymbols) {
        // Allow for the use of unqualified and qualified names
        ctx.symbols[unqualifiedName].push_back(symbolInfo);
        ctx.symbols[symbolInfo.qualifiedName].push_back(symbolInfo);
    }
    for (auto const& dep : currentModule.dependencies) {
        auto const& dependentModule = allModules.at(dep);
        for (auto const& [unqualifiedName, symbolInfo] :
             dependentModule.exportedSymbols) {
            // Allow for the use of unqualified and qualified names
            ctx.symbols[unqualifiedName].push_back(symbolInfo);
            ctx.symbols[symbolInfo.qualifiedName].push_back(symbolInfo);
        }
    }

    // Iterate over AST
    for (auto& decl : currentModule.ast->decls) {
        std::visit(Overloaded{
                       [&](AstMessage& msg) {
                           resolveMessage(errors, ctx, msg.block);
                       },
                       // Ignore the rest of the cases
                       [](AstImport const&) {},
                       [](AstPackageDecl const&) {},
                       [](AstDefault const&) {},
                   },
                   decl.decl);
    }
}

bool SemanticContext::resolveSymbols() {
    // Setup module names
    for (auto& [fileName, module] : m_modules)
        exportSymbols(m_errors, m_symbolTable, module);

    for (auto& [fileName, module] : m_modules)
        resolveModuleSymbols(m_errors, module, m_modules);
    if (m_errors.errors.size() > 0)
        return false;
    return true;
}

bool SemanticContext::validateIds() {
    auto globalIds = validateGlobalMessageIds(m_errors, m_modules);
    auto localIds = validateFieldNumbers(m_errors, m_modules);
    auto validFieldNames = validateFieldNames(m_errors, m_modules);
    return globalIds && localIds && validFieldNames;
}

bool SemanticContext::computeDirectives() {
    ::computeDirectives(m_errors, m_modules);
    validateAstTypeProperties(m_errors, m_modules);
    return m_errors.errors.size() == 0;
}

}  // namespace ao::schema
