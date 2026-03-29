#include "ao/schema/CodecCommon.h"

#include <algorithm>

#include "ao/schema/IR.h"
#include "ao/utils/Overloaded.h"

namespace ao::schema::codec {
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
                [&](IdFor<ir::Enum> const& e) {
                    auto const& desc = ir.enums[e.idx];
                    return CodecType{
                        .bitWidth = (uint8_t)std::clamp(
                            1ull, (uint64_t)std::bit_width(desc.fields.size()),
                            64ull),
                        .flags = 0,
                    };
                },
            },
            type.payload);
        ret.types.emplace_back(entry);
    }

    for (auto& field : ir.fields) {
        ret.fields.push_back(CodecField{
            .fieldNumber = field.fieldNumber,
            .typeId = (uint32_t)field.type.idx,
        });
    }

    for (auto& oneofs : ir.oneOfs) {
        ret.oneofs.push_back(CodecOneof{
            .fieldStart = (uint32_t)ret.oneofFieldNumbers.size(),
            .fieldCount = (uint32_t)oneofs.arms.size(),
        });

        for (auto const& arm : oneofs.arms) {
            auto const& field = ir.fields[arm.idx];
            ret.oneofFieldNumbers.push_back(field.fieldNumber);
        }
    }

    return ret;
}
}  // namespace ao::schema::codec
