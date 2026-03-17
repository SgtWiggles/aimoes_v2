#include "ao/schema/Ast.h"

#include <ranges>

namespace ao::schema {
AstQualifiedName parseQualifiedName(std::string_view path) {
    AstQualifiedName ret;
    for (auto part : path | std::views::split('.')) {
        ret.name.push_back(std::string(part.begin(), part.end()));
    }
    return ret;
}
}  // namespace ao::schema
