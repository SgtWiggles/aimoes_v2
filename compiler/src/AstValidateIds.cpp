#include "AstValidateIds.h"

#include "ao/utils/Overloaded.h"

#include <variant>

using namespace ao;
using namespace ao::schema;

bool validateGlobalMessageIds(
    ao::schema::ErrorContext& errs,
    std::unordered_map<std::string, ao::schema::CompilerContext::Module>&
        modules) {
    std::unordered_map<uint64_t, AstMessage*> globalMessages;
    // Fill message id to message list
    for (auto& [path, module] : modules) {
        for (auto& decl : module.ast->decls) {
            std::optional<uint64_t> msgId;
            AstMessage* msgPtr;
            std::visit(Overloaded{
                           [](AstImport const&) {},
                           [](AstPackageDecl const&) {},
                           [&](AstMessage& msg) {
                               msgId = msg.messageId;
                               msgPtr = &msg;
                           },
                           [](AstDefault const&) {},
                       },
                       decl.decl);
            if (!msgId)
                continue;
            if (globalMessages.contains(*msgId)) {
                auto const& prev = globalMessages.at(msgId.value());
                errs.require(
                    false, {
                               ErrorCode::MULTIPLY_DEFINED_MESSAGE_ID,
                               std::format("Message with id {} was "
                                           "already defined at {}:{}:{}",
                                           *msgId, prev->loc.file,
                                           prev->loc.lineNumber, prev->loc.col),
                               msgPtr->loc,
                           });
            } else {
                globalMessages[*msgId] = msgPtr;
                module.messagesById[*msgId] = msgPtr;
            }
        }
    }

    return errs.errors.size() == 0;
}



void validateMessageFieldsNumbers(ao::schema::ErrorContext& errs,
                                  AstMessageBlock& message) {
    auto& idList = message.fieldsByFieldId;
    // Process reserved first
    for (auto& fieldDecl : message.fields) {
        std::visit(Overloaded{
                       [&](AstFieldReserved const& reserved) {
                           // Overlapping reserved doesn't cause issues
                           for (auto id : reserved.fieldNumbers) {
                               auto [iter, inserted] =
                                   idList.try_emplace(id, &fieldDecl);
                           }
                       },
                       [](AstFieldOneOf const&) {},
                       [](AstDefault const&) {},
                       [](AstField const&) {},
                   },
                   fieldDecl.field);
    }

    auto insertFieldId = [&](uint64_t id, AstFieldDecl* decl,
                             SourceLocation loc) {
        auto [iter, inserted] = idList.try_emplace(id, decl);
        errs.require(
            inserted,
            {ErrorCode::MULTIPLY_DEFINED_FIELD_ID,
             std::format("Field ID {} was already defined at {}:{}:{}", id,
                         iter->second->loc.file, iter->second->loc.lineNumber,
                         iter->second->loc.col),
             loc});
        return inserted;
    };
    // Then process the actual fields
    for (auto& fieldDecl : message.fields) {
        std::visit(
            Overloaded{
                [&](AstField& field) {
                    insertFieldId(field.fieldNumber, &fieldDecl, field.loc);
                },
                [&](AstFieldOneOf& field) {
                    insertFieldId(field.fieldNumber, &fieldDecl, field.loc);
                    // Process field IDs of the oneof
                },
                [](AstFieldReserved const&) {},
                [](AstDefault const&) {},
            },
            fieldDecl.field);
    }
}

bool validateFieldNumbers(
    ao::schema::ErrorContext& errs,
    std::unordered_map<std::string, ao::schema::CompilerContext::Module>&
        modules) {
    for (auto& [path, module] : modules) {
        for (auto& decl : module.ast->decls) {
            std::visit(Overloaded{
                           [](AstImport const&) {},
                           [](AstPackageDecl const&) {},
                           [&](AstMessage& msg) {
                               validateMessageFieldsNumbers(errs, msg.block);
                           },
                           [](AstDefault const&) {},
                       },
                       decl.decl);
        }
    }
    return errs.errors.size() == 0;
}
