#pragma once
#include <cstdint>
#include <span>

namespace ao::pack {

// Hasher concept: requires update(ptr, len), digest() and reset()
template <class H>
concept HasherConcept = requires(H h, std::span<std::byte const> v) {
    { h.update(v) } -> std::same_as<void>;
    { h.digest() };
    { h.reset() } -> std::same_as<void>;
};

// HashingStream: forwards to an underlying Stream but updates a running hash
template <class Stream, HasherConcept Hasher>
class HashingStream {
   public:
    HashingStream(Stream& s, Hasher hasher = {})
        : m_hasher(std::move(hasher)), m_inner(s), m_enabled(false) {}

    // Forwarded ok() and require(...) used by serializers
    bool ok() const { return m_inner.ok(); }
    template <class... Args>
    void require(Args&&... args) {
        m_inner.require(std::forward<Args>(args)...);
    }

    // Generic bytes() forwarder that updates the hash (when enabled) and
    // forwards
    template <class Span>
    void bytes(Span span, size_t count) {
        m_inner.bytes(span, count);
        if (m_enabled && count > 0) {
            // construct a const span of the requested size
            auto const* dataPtr = span.data();
            std::span<std::byte const> s((std::byte const*)dataPtr, count);
            m_hasher.update(s);
        }
    }

    // convenience
    auto digest() const noexcept { return m_hasher.digest(); }
    void disableHashing() noexcept { m_enabled = false; }
    void enableHashing() noexcept {
        m_enabled = true;
        m_hasher.reset();
    }

    Stream& inner() noexcept { return m_inner; }

   private:
    Hasher m_hasher;
    Stream& m_inner;
    bool m_enabled;
};

}  // namespace ao::pack
