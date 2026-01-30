#pragma once

#include <cstdint>
#include <vector>

#include <ao/pack/ByteStream.h>

namespace ao::pack::disk {
/*
Base disk format types

FIXED (k) for k <= 8
UVAR 
SVAR 
LEN <Repeated contents>

TAG is message type
LEN is length of VALUE in bytes
VALUE is parsed based on tag
TAG LEN VALUE

We are treating sub-messages as raw byte arrays effectively.
Tags must be serialized in ascending order per message.
Must be enforced during encoding and decoding to ensure hash stability in log.

Envelope format for disk, each item must be COBS encoded on disk
<Message Id> <Payload> <CRC32 of PAYLOAD>
*/

struct EncoderContext {
    std::vector<std::byte> data;
    std::vector<uint32_t> sizes;
};

class Encoder {
   public:
    Encoder(EncoderContext& ctx)
        : m_ctx(ctx),
          m_sizeStream(),
          m_dataStream() {}

    // TODO base class here with message
    void encode() {

    }

   private:
    EncoderContext& m_ctx;
};

class Decoder {
   public:
   private:
}
}  // namespace ao::pack::disk
