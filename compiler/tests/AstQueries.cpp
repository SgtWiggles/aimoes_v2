#include "AstQueries.h"

using namespace ao::schema;

namespace ao::schema::query {

AstMessage* findMessageByUnresolvedName(std::shared_ptr<AstFile> file,
                                        std::string const& unresolvedName) {
    if (!file)
        return nullptr;
    for (auto& decl : file->decls) {
        if (auto msg = std::get_if<AstMessage>(&decl.decl)) {
            if (msg->name == unresolvedName)
                return msg;
        }
    }
    return nullptr;
}

AstMessage* findMessageInModules(
    std::unordered_map<std::string, ao::schema::SemanticContext::Module> const&
        modules,
    std::string const& moduleName,
    std::string const& messageName) {
    auto it = modules.find(moduleName);
    if (it == modules.end())
        return nullptr;
    return findMessageByUnresolvedName(it->second.ast, messageName);
}

AstField* findFieldByName(AstMessage& message, std::string const& fieldName) {
    for (auto& fd : message.block.fields) {
        if (auto pf = std::get_if<AstField>(&fd.field)) {
            if (pf->name == fieldName)
                return pf;
        }
    }
    return nullptr;
}

AstField* findFieldInModule(
    std::unordered_map<std::string, ao::schema::SemanticContext::Module> const&
        modules,
    std::string const& moduleName,
    std::string const& messageName,
    std::string const& fieldName) {
    auto msg = findMessageInModules(modules, moduleName, messageName);
    if (!msg)
        return nullptr;
    return findFieldByName(*msg, fieldName);
}

bool hasPackageDecl(ao::schema::AstFile const& file,
                    AstQualifiedName const& name) {
    for (auto const& decl : file.decls) {
        auto* pkg = std::get_if<AstPackageDecl>(&decl.decl);
        if (!pkg)
            continue;
        if (pkg->name.name == name.name)
            return true;
    }

    return false;
}
bool hasPackageDecl(ao::schema::AstFile const& file) {
    for (auto const& decl : file.decls) {
        auto* pkg = std::get_if<AstPackageDecl>(&decl.decl);
        if (!pkg)
            continue;
        return true;
    }

    return false;
}

}  // namespace ao::schema::query