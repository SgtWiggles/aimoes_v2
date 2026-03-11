#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "ao/pack/ByteStream.h"
#include "ao/pack/Error.h"
#include "ao/pack/Varint.h"
#include "ao/pack/ZigZag.h"
#include "ao/schema/CodecCommon.h"

namespace ao::schema::codec::disk {

template <class OutStream>
class DiskEncodeCodec {
   public:
   private:
};

template <class InStream>
class DiskDecodeCodec {
   private:
   private:
};

/*
static_assert(CodecEncode<DiskEncodeCodec<ao::pack::byte::WriteStream>>);
static_assert(CodecDecode<DiskDecodeCodec<ao::pack::byte::ReadStream>>);
*/
}  // namespace ao::schema::codec::disk
