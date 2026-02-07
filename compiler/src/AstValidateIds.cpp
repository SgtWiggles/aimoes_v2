#include "AstValidateIds.h"

#include "ao/utils/Overloaded.h"

#include <variant>

using namespace ao;
using namespace ao::schema;

bool validateGlobalMessageIds(
    ao::schema::ErrorContext& errs,
    std::unordered_map<std::string, ao::schema::SemanticContext::Module>&
        modules) {
    std::unordered_map<uint64_t, AstMessage*> globalMessages;
    // Fill message id to message list
    for (auto& [path, module] : modules) {
        for (auto& decl : module.ast->decls) {
            std::optional<uint64_t> msgId;
            AstMessage* msgPtr = nullptr;
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
                errs.require(false, {
                                        ErrorCode::MULTIPLY_DEFINED_MESSAGE_ID,
                                        std::format("Message with id {} was "
                                                    "already defined at {}",
                                                    *msgId, prev->loc),
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
                                  AstMessageBlock& message);
void validateMessageFieldsNumbers(ao::schema::ErrorContext& errs,
                                  AstType& type) {
    for (auto const& subtype : type.subtypes) {
        if (!subtype)
            continue;
        validateMessageFieldsNumbers(errs, *subtype);
    }
    validateMessageFieldsNumbers(errs, type.block);
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
                       [](AstDefault const&) {},
                       [](AstField const&) {},
                   },
                   fieldDecl.field);
    }

    auto insertFieldId = [&](uint64_t id, AstFieldDecl* decl,
                             SourceLocation loc) {
        auto [iter, inserted] = idList.try_emplace(id, decl);
        errs.require(inserted,
                     {ErrorCode::MULTIPLY_DEFINED_FIELD_ID,
                      std::format("Field ID {} was already defined at {}", id,
                                  iter->second->loc),
                      loc});
        return inserted;
    };
    // Then process the actual fields
    for (auto& fieldDecl : message.fields) {
        std::visit(Overloaded{
                       [&](AstField& field) {
                           insertFieldId(field.fieldNumber, &fieldDecl,
                                         field.loc);
                           validateMessageFieldsNumbers(errs, field.typeName);
                       },
                       [](AstFieldReserved const&) {},
                       [](AstDefault const&) {},
                   },
                   fieldDecl.field);
    }
}

bool validateFieldNumbers(
    ao::schema::ErrorContext& errs,
    std::unordered_map<std::string, ao::schema::SemanticContext::Module>&
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

void validateFieldNames(ao::schema::ErrorContext& errs,
                        ao::schema::AstMessageBlock const& blk);

void validateFieldNames(ao::schema::ErrorContext& errs,
                        ao::schema::AstType const& type) {
    for (auto const& subtype : type.subtypes) {
        errs.require((bool)subtype,
                     {ErrorCode::INTERNAL, "Got nullptr on subtype", type.loc});
        if (!subtype) {
            validateFieldNames(errs, *subtype);
        }
    }
    validateFieldNames(errs, type.block);
}

void validateFieldNames(ao::schema::ErrorContext& errs,
                        ao::schema::AstMessageBlock const& blk) {
    std::unordered_map<std::string, SourceLocation> definedFields;
    auto addFieldName = [&definedFields, &errs](std::string const& str,
                                                SourceLocation const& loc) {
        auto [iter, inserted] = definedFields.try_emplace(str, loc);
        errs.require(inserted,
                     {
                         .code = ErrorCode::MULTIPLY_DEFINED_SYMBOL,
                         .message = std::format(
                             "Multiple declarations of field with name '{}'. "
                             "Previously declared at: {}",
                             str, iter->second),
                         .loc = loc,
                     });
    };
    for (auto const& fieldDecl : blk.fields) {
        std::visit(Overloaded{
                       [&](AstField const& field) {
                           addFieldName(field.name, field.loc);
                           validateFieldNames(errs, field.typeName);
                       },
                       // Ignore these cases
                       [](AstFieldReserved const&) {},
                       [](AstDefault const&) {},
                   },
                   fieldDecl.field);
    }
}

bool validateFieldNames(
    ao::schema::ErrorContext& errors,
    std::unordered_map<std::string, ao::schema::SemanticContext::Module>&
        modules) {
    for (auto& [path, module] : modules) {
        for (auto& decl : module.ast->decls) {
            std::visit(Overloaded{
                           [](AstImport const&) {},
                           [](AstPackageDecl const&) {},
                           [&](AstMessage& msg) {
                               validateFieldNames(errors, msg.block);
                           },
                           [](AstDefault const&) {},
                       },
                       decl.decl);
        }
    }

    return errors.errors.size() == 0;
}
