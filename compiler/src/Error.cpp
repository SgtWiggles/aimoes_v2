#include "ao/schema/Error.h"

namespace ao::schema {
std::string ErrorContext::toString() const {
    std::stringstream ss;
    for (auto const& err : errors) {
        ss << std::format("ERROR ({}): {} at {}", (uint64_t)err.code, err.message,
                          err.loc);
    }
    return ss.str();
}
}  // namespace ao::schema
