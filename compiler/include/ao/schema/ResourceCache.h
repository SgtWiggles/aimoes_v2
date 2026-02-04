#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ao::schema {
template <class T>
struct IdFor {
    uint64_t idx;
    auto operator<=>(const IdFor<T>& other) const = default;
};

template <class T, class Hash = std::hash<T>, class Eq = std::equal_to<T>>
class ResourceCache {
   public:
    IdFor<T> getId(T value) {
        auto [iter, inserted] = m_ids.try_emplace(value, m_values.size());
        if (inserted)
            m_values.emplace_back(std::move(value));
        return {iter->second};
    }

    std::vector<T> const& values() const { return m_values; }

   private:
    std::unordered_map<T, uint64_t, Hash, Eq> m_ids;
    std::vector<T> m_values;
};

}  // namespace ao::schema
