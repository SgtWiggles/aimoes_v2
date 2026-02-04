#include "ao/schema/IR.h"

#include "ao/schema/ResourceCache.h"

namespace ao::schema::ir {

struct IRContext {
    ResourceCache<std::string> strings;
    ResourceCache<DirectiveProperty> directiveProperties;
    ResourceCache<DirectiveProfile> directiveProfiles;
    ResourceCache<DirectiveSet> directiveSets;
    ResourceCache<OneOf> oneOfs;
    ResourceCache<Field> fields;
    ResourceCache<Message> messages;
    ResourceCache<Type> types;
};

IR generateIR(
    std::unordered_map<std::string, ao::schema::SemanticContext::Module> const&
        modules) {
    return IR();
}
}  // namespace ao::schema::ir
