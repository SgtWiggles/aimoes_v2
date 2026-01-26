#pragma once

#include <cstdint>

namespace ao::pack {
inline uint64_t encodeZigZag(int64_t n) {
    return (static_cast<uint64_t>(n) << 1) ^ (static_cast<uint64_t>(n >> 63));
}
inline int64_t decodeZigZag(uint64_t n) {
    return (static_cast<int64_t>(n >> 1)) ^
           (static_cast<int64_t>(-(static_cast<int64_t>(n) & 1)));
}

}  // namespace ao::pack
