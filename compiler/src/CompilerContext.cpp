#include "ao/schema/CompilerContext.h"
#include "ao/schema/Error.h"

#include <format>
#include <queue>
#include <sstream>
#include <unordered_set>
#include <variant>

namespace ao::schema {

struct ModuleLoadContext {
    std::string resolvedPath;
    AstImport import;
};

bool CompilerContext::loadFile(std::string rootPath) {
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
            .lineNumber = 0,
            .col = 0,
        },
    });

    std::unordered_map<std::string, std::shared_ptr<AstFile>> pendingFiles;
    std::unordered_map<std::string, std::unordered_map<std::string, AstImport>>
        dependedOnBy;

    // Computes the inverted dependency graph
    while (!toResolve.empty()) {
        auto currentResolve = toResolve.front();
        auto current = std::get<0>(currentResolve);
        toResolve.pop();

        // Module is already fully resolved
        if (pendingFiles.find(current) != pendingFiles.end())
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
                ss << std::format("at {}:{}:{} for file {}",
                                  fromImport.loc.file,
                                  fromImport.loc.lineNumber, fromImport.loc.col,
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

    for (auto const& [path, module] : pendingFiles)
        m_modules[path].ast = module;

    return true;
}
}  // namespace ao::schema