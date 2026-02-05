#pragma once
#include <boost/container_hash/hash.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ao::schema {
template <class T>
struct IdFor {
    uint64_t idx = std::numeric_limits<uint64_t>::max();

    auto operator<=>(IdFor<T> const& other) const = default;
    operator bool() const { return this->valid(); }
    uint64_t valid() const {
        return idx != std::numeric_limits<uint64_t>::max();
    }
};
template <class T>
size_t hash_value(IdFor<T> const& v) {
    return boost::hash_value(v.idx);
}

template <class T, class Hash = boost::hash<T>, class Eq = std::equal_to<T>>
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
template <class T,
          class Value,
          class Hash = boost::hash<T>,
          class Eq = std::equal_to<T>>
class KeyedResourceCache {
   public:
    IdFor<Value> getId(T key) {
        auto [iter, inserted] = m_ids.try_emplace(key, m_values.size());
        if (inserted)
            m_values.emplace_back();
        return {iter->second};
    }
    IdFor<Value> getId(T k, Value v) {
        auto id = getId(k);
        value(id) = std::move(v);
        return id;
    }

    Value& value(IdFor<Value> value) { return m_values[value.idx]; }
    Value const& value(IdFor<Value> value) const { return m_values[value.idx]; }

    std::vector<Value> const& values() const { return m_values; }

   private:
    std::unordered_map<T, uint64_t, Hash, Eq> m_ids;
    std::vector<Value> m_values;
};

}  // namespace ao::schema
