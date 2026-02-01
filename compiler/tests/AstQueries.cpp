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
    std::unordered_map<std::string, ao::schema::CompilerContext::Module> const&
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
        } else if (auto po = std::get_if<AstFieldOneOf>(&fd.field)) {
            for (auto& inner : po->block.fields) {
                if (auto pif = std::get_if<AstField>(&inner.field)) {
                    if (pif->name == fieldName)
                        return pif;
                }
            }
        }
    }
    return nullptr;
}

AstFieldOneOf* findOneOfByName(AstMessage& message,
                               std::string const& oneOfName) {
    for (auto& fd : message.block.fields) {
        if (auto po = std::get_if<AstFieldOneOf>(&fd.field)) {
            if (po->name == oneOfName)
                return po;
        }
    }
    return nullptr;
}

AstField* findFieldInModule(
    std::unordered_map<std::string, ao::schema::CompilerContext::Module> const&
        modules,
    std::string const& moduleName,
    std::string const& messageName,
    std::string const& fieldName) {
    auto msg = findMessageInModules(modules, moduleName, messageName);
    if (!msg)
        return nullptr;
    return findFieldByName(*msg, fieldName);
}

}  // namespace ao::schema::query