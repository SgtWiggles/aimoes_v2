#include "ao/schema/Codec.h"

#include <algorithm>

#include "ao/utils/Overloaded.h"

namespace ao::schema::vm {
CodecTable generateCodecTable(ir::IR const& ir) {
    CodecTable ret;
    for (auto& type : ir.types) {
        auto entry = std::visit(
            Overloaded{
                [](ir::Scalar const& scalar) {
                    return CodecType{
                        .bitWidth =
                            (uint8_t)std::clamp(1ull, scalar.width, 64ull),
                        .flags = 0,
                    };
                },
                [](ir::Array const& arr) {
                    return CodecType{
                        .bitWidth =
                            (uint8_t)std::bit_width(arr.maxSize.value_or(0)),
                        .flags = 0,
                    };
                },
                [](ir::Optional const& opt) { return CodecType{}; },
                [&](IdFor<ir::OneOf> const& oneof) {
                    auto const& desc = ir.oneOfs[oneof.idx];
                    return CodecType{
                        .bitWidth = (uint8_t)std::bit_width(desc.arms.size()),
                        .flags = 0,
                    };
                },
                [&](IdFor<ir::Message> const& message) { return CodecType{}; },
            },
            type.payload);
        ret.types.emplace_back(entry);
    }

    // TODO build messages, for now we only need types
    return ret;
}
}  // namespace ao::schema::vm
