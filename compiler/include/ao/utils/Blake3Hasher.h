#pragma once
#include <array>
#include <cstddef>
#include <span>

#include "ao/pack/HashingStream.h"

#include <blake3.h>

namespace ao::utils::hash {
class Blake3Hasher {
   public:
    using Hash = std::array<std::byte, 32>;

    Blake3Hasher() { reset(); }

    void update(std::span<std::byte const> v) {
        blake3_hasher_update(
            &m_hasher, reinterpret_cast<uint8_t const*>(v.data()), v.size());
    }

    Hash digest() const {
        Hash out{};
        blake3_hasher_finalize(
            &m_hasher, reinterpret_cast<uint8_t*>(out.data()), out.size());
        return out;
    }

    void reset() { blake3_hasher_init(&m_hasher); }

   private:
    blake3_hasher m_hasher{};
};
static_assert(ao::pack::HasherConcept<Blake3Hasher>);

}  // namespace ao::utils::hash
