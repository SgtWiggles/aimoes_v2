#pragma once

#include "ao/schema/Ast.h"
#include "ao/schema/SemanticContext.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace ao::schema::query {

// Find first `AstMessage` in `file` whose unresolved name equals
// `unresolvedName`. Returns nullptr if not found.
ao::schema::AstMessage* findMessageByUnresolvedName(
    std::shared_ptr<ao::schema::AstFile> file,
    std::string const& unresolvedName);

// Find a message by name in a particular module (by module unique name).
// Returns nullptr if not found.
ao::schema::AstMessage* findMessageInModules(
    std::unordered_map<std::string, ao::schema::SemanticContext::Module> const&
        modules,
    std::string const& moduleName,
    std::string const& messageName);

// Find a top-level `AstField` (or inner oneof field) by `fieldName` inside
// `message`. Returns nullptr if not found.
ao::schema::AstField* findFieldByName(ao::schema::AstMessage& message,
                                      std::string const& fieldName);

// Find an `AstFieldOneOf` by its oneof name inside `message`. nullptr if not
// found.
ao::schema::AstFieldOneOf* findOneOfByName(ao::schema::AstMessage& message,
                                           std::string const& oneOfName);

// Convenience: find a field by name inside a given module/message.
// Returns nullptr if module, message or field is not found.
ao::schema::AstField* findFieldInModule(
    std::unordered_map<std::string, ao::schema::SemanticContext::Module> const&
        modules,
    std::string const& moduleName,
    std::string const& messageName,
    std::string const& fieldName);

}  // namespace ao::schema::query