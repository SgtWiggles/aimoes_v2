#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ao/schema/IR.h>
#include <ao/utils/Array.h>

inline void replaceAll(std::string& str,
                std::string_view key,
                std::string_view value) {
    if (key.empty())
        return;  // avoid infinite loop

    size_t pos = 0;
    while ((pos = str.find(key, pos)) != std::string::npos) {
        str.replace(pos, key.length(), value);
        pos += value.length();  // advance past the replacement
    }
}
inline std::string replaceMany(
    std::string_view str,
    std::initializer_list<std::pair<std::string_view, std::string_view>>
        replacements) {
    auto ret = std::string{str};
    for (const auto& [key, value] : replacements) {
        replaceAll(ret, key, value);
    }
    return ret;
}
inline std::vector<std::string_view> parsePackageName(std::string_view str) {
    std::vector<std::string_view> output;
    size_t lastWindow = 0;
    for (size_t idx = 0; idx < str.size(); ++idx) {
        if (str[idx] != '.')
            continue;
        auto count = idx - lastWindow;
        output.emplace_back(str.substr(lastWindow, count));
        lastWindow = idx + 1;
    }
    if (lastWindow < str.size()) {
        auto finalName = str.substr(lastWindow, str.size() - lastWindow);
        output.emplace_back(finalName);
    }
    return output;
}
inline std::optional<std::string_view> getMessageName(std::string_view name) {
    auto parts = parsePackageName(name);
    if (parts.empty())
        return {};
    return parts.back();
}
inline std::optional<std::string> getNamespaceName(std::string_view name) {
    auto packageName = parsePackageName(name);
    if (packageName.empty())
        return {};

    auto namespaceName = std::string{};
    for (size_t idx = 0; idx < packageName.size() - 1; ++idx) {
        if (!namespaceName.empty())
            namespaceName += "::";
        namespaceName += packageName[idx];
    }

    return namespaceName;
}


struct TypeName {
    std::string ns;
    std::string name;
    std::string qualifiedName() const {
        if (ns.empty())
            return name;
        return std::format("{}::{}", ns, name);
    }
};

struct GeneratedObject {
    TypeName name;
    std::string decl;
    std::string impl;
};

struct CppCodeGenContext {
    ao::schema::ir::IR const& ir;
    ao::schema::ErrorContext& errs;

    std::vector<TypeName> generatedTypeNames;
    std::vector<GeneratedObject> generatedAccessors;
    std::vector<std::string> generatedTypeDecls;
    std::vector<std::string> generatedTypeDefs;
};


inline uint8_t getCppBitWidth(CppCodeGenContext& ctx, uint64_t width) {
    // default to uint64_t if no width specified, this is
    // consistent with the codec table generation
    if (width == 0)
        return 64;

    static constexpr auto items = ao::makeArray<int>(8, 16, 32, 64);
    for (auto item : items) {
        if (width <= item)
            return static_cast<uint8_t>(item);
    }

    ctx.errs.fail({
        .code = ao::schema::ErrorCode::INTERNAL,
        .message = std::format("Unsupported bit width for C++: {}", width),
        .loc = {},
    });
    return 0;
}


GeneratedObject generateAccessorDecl(CppCodeGenContext& ctx,
                                 size_t typeId,
                                 ao::schema::ir::Type const& type);
