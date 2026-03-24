#include "CppTypeAccessor.h"

#include "ao/utils/Array.h"

void generateTypeAccessorOneof(CppCodeGenContext& ctx,
                               size_t typeId,
                               ao::schema::ir::OneOf const& oneofDesc) {
    auto const& typeName = ctx.generatedTypeNames[typeId];
    auto& accessor = ctx.generatedAccessors[typeId];

    // TODO abstract this armid into another lamba.
    // Maybe even abstract C++ into something more token based to
    // ensure we don't have mismatch braces
    std::string encodeOneofEnterArm = replaceMany(
        R"(
void encodeOneofEnterArm_@TYPE_ID(
 ao::schema::cpp::CppEncodeRuntime& runtime,
 ao::schema::cpp::AnyPtr ptr,
	uint32_t oneofId,
	uint32_t armId) {
 auto const& data = ptr.as<@TYPE_NAME>();
 if (data.index() != armId +1) {
 ao::schema::cpp::cppRuntimeFail(runtime, ao::pack::Error::BadData)
 return;
 }

 switch (armId) {
)",
        {
            {"@TYPE_NAME", typeName.qualifiedName()},
            {"@TYPE_ID", std::to_string(typeId)},
        });
    ao::enumerate(
        oneofDesc.arms,
        [&](size_t idx, ao::schema::IdFor<ao::schema::ir::Field> fieldId) {
            auto const& fieldDesc = ctx.ir.fields[fieldId.idx];
            encodeOneofEnterArm +=
                replaceMany(R"(
		case @FIELD_ID: {
			auto encodePtr = &@SUBTYPE_ACCESSOR::encode;
			auto dataPtr = (void const*)(&std::get<@FIELD_ID +1>(data));
			runtime.stack.emplace_back(ao::schema::encode::EncodeFrame{
				.ops = encodePtr,
				.data = ao::schema::cpp::AnyPtr{dataPtr},
			});
		} break;
)",
                            {
                                {"@FIELD_ID", std::to_string(idx)},
                                {
                                    "@SUBTYPE_ACCESSOR",
                                    ctx.generatedAccessors[fieldDesc.type.idx]
                                        .name.qualifiedName(),
                                },
                            });
        });

    encodeOneofEnterArm += R"(
		default: {
			ao::schema::cpp::cppRuntimeFail(runtime, ao::pack::Error::BadData);
			return;
		} break;
 }
})";

    std::string decodeOneofIndex =
        replaceMany(R"(
void decodeOneofIndex_@TYPE_ID(
 ao::schema::cpp::CppDecodeRuntime& runtime,
 ao::schema::cpp::MutPtr ptr,
 uint32_t oneofId,
 uint32_t armId) {
 auto& data = ptr.as<@TYPE_NAME>();
 switch(armId) {
)",
                    {
                        {"@TYPE_NAME", typeName.qualifiedName()},
                        {"@TYPE_ID", std::to_string(typeId)},
                    });

    ao::enumerate(
        oneofDesc.arms,
        [&](size_t idx, ao::schema::IdFor<ao::schema::ir::Field> fieldId) {
            auto const& fieldDesc = ctx.ir.fields[fieldId.idx];
            encodeOneofEnterArm += replaceMany(
                R"(
		case @FIELD_ID: {
			auto ops = &@SUBTYPE_ACCESSOR::decode;
 data.emplace<@FIELD_ID +1>();
		} break;
)",
                {
                    {"@FIELD_ID", std::to_string(idx)},
                    {
                        "@SUBTYPE_ACCESSOR",
                        ctx.generatedAccessors[fieldDesc.type.idx]
                            .name.qualifiedName(),
                    },
                });
        });
    decodeOneofIndex += R"(
		default: {
			ao::schema::cpp::cppRuntimeFail(runtime, ao::pack::Error::BadData);
			return;
		} break;
 }
}
)";
    std::string decodeOneofEnterArm =
        replaceMany(R"(
void decodeOneofEnterArm(
 ao::schema::cpp::CppDecodeRuntime& runtime,
	ao::schema::cpp::MutPtr ptr,
	uint32_t oneofId,
	uint32_t armId) {
 auto& data = ptr.as<@TYPE_NAME>();
 switch(armId) {
)",
                    {
                        {"@TYPE_NAME", typeName.qualifiedName()},
                    });

    ao::enumerate(
        oneofDesc.arms,
        [&](size_t idx, ao::schema::IdFor<ao::schema::ir::Field> fieldId) {
            auto const& fieldDesc = ctx.ir.fields[fieldId.idx];
            decodeOneofEnterArm += replaceMany(
                R"(
		case @FIELD_ID: {
			auto ops = &@SUBTYPE_ACCESSOR::decode;
 auto value = std::get_if<@FIELD_ID +1>(&data);
			runtime.stack.emplace_back(ao::schema::cpp::DecodeFrame{
				.ops = ops,
				.data = ao::schema::cpp::MutPtr{(void*)value},
			});
		} break;
)",
                {
                    {"@FIELD_ID", std::to_string(idx)},
                    {
                        "@SUBTYPE_ACCESSOR",
                        ctx.generatedAccessors[fieldDesc.type.idx]
                            .name.qualifiedName(),
                    },
                });
        });

    decodeOneofEnterArm += R"(
		default: {
			ao::schema::cpp::cppRuntimeFail(runtime, ao::pack::Error::BadData);
			return;
		} break;
 }
}
)";

    accessor.impl = replaceMany(R"(
uint32_t encodeOneofIndex_@TYPE_ID(
 ao::schema::cpp::CppEncodeRuntime& runtime,
 ao::schema::cpp::AnyPtr ptr,
 uint32_t oneofId,
 uint32_t width) {
 auto& data = ptr.as<@TYPE_NAME>();
 auto idx = data.index();
 if (idx ==0)
 return std::numeric_limits<uint32_t>::max();
 return idx -1;
}
void encodeOneofEnter_@TYPE_ID(
 ao::schema::cpp::CppEncodeRuntime& runtime,
 ao::schema::cpp::AnyPtr ptr,
 uint32_t oneofId) {
 // do nothing
}
void encodeOneofExit_@TYPE_ID(
 ao::schema::cpp::CppEncodeRuntime& runtime,
 ao::schema::cpp::AnyPtr ptr) {
 // do nothing
}

@ENTER_ENCODE_ARM

void encodeOneofExitArm_@TYPE_ID(
 ao::schema::cpp::CppEncodeRuntime& runtime,
 ao::schema::cpp::AnyPtr ptr) {
 runtime.stack.pop_back();
}


void decodeOneofEnter_@TYPE_ID(
 ao::schema::cpp::CppDecodeRuntime& runtime,
 ao::schema::cpp::MutPtr ptr,
 uint32_t oneofId) {
 
}
void decodeOneofExit_@TYPE_ID(
 ao::schema::cpp::CppDecodeRuntime& runtime,
 ao::schema::cpp::MutPtr ptr) {

}

@DECODE_INDEX

@DECODE_ENTER_ARM

void decodeOneofExitArm_@TYPE_ID(
 ao::schema::cpp::CppDecodeRuntime& runtime,
 ao::schema::cpp::MutPtr ptr) {
 runtime.stack.pop_back();
}


ao::schema::cpp::EncodeTypeOps @QNAME::encode = ao::schema::cpp::EncodeTypeOps{
 .oneofIndex = &encodeOneofIndex_@TYPE_ID,
 .oneofEnter = &encodeOneofEnter_@TYPE_ID,
 .oneofExit = &encodeOneofEnter_@TYPE_ID,
 .oneofEnterArm = &encodeOneofEnterArm_@TYPE_ID,
 .oneofExitArm = &encodeOneofExitArm_@TYPE_ID,
};
ao::schema::cpp::DecodeTypeOps @QNAME::decode = ao::schema::cpp::DecodeTypeOps{
	.oneofEnter = &decodeOneofEnter_@TYPE_ID,
	.oneofExit = &decodeOneofExit_@TYPE_ID,
	.oneofIndex = &decodeOneofIndex_@TYPE_ID,
	.oneofEnterArm = &decodeOneofEnterArm_@TYPE_ID,
	.oneofEnterArm = &decodeOneofExitArm_@TYPE_ID,
};
)",
                                {
                                    {"@TYPE_NAME", typeName.qualifiedName()},
                                    {"@ENCODE_ENTER_ARM", encodeOneofEnterArm},
                                    {"@DECODE_INDEX", decodeOneofIndex},
                                    {"@DECODE_ENTER_ARM", decodeOneofEnterArm},
                                    {"@QNAME", accessor.name.qualifiedName()},
                                    {"@TYPE_ID", std::to_string(typeId)},
                                });
}
