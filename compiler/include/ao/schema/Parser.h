#pragma once
#include <string>
#include <string_view>

namespace ao::schema {
bool parseMatch(std::string_view str, std::string* errsOut = nullptr);
}