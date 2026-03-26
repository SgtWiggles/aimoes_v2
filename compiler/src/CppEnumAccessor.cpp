#include "CppTypeAccessor.h"

void generateTypeAccessorEnum(CppCodeGenContext& ctx,
                          size_t typeId,
                          ao::schema::IdFor<ao::schema::ir::Enum> e) {
    auto& accessor = ctx.generatedAccessors[typeId];
    auto qname = accessor.name.qualifiedName();
    auto generatedType = ctx.generatedTypeNames[typeId].qualifiedName();

    accessor.impl = replaceMany(R"(
int64_t encode_@OP_NAME_@TYPE_ID(
    ao::schema::cpp::CppEncodeRuntime& runtime,
    ao::schema::cpp::AnyPtr ptr,
    uint16_t width) {
    return (int64_t)ptr.as<@GENERATED_TYPE>();
}
void decode_@OP_NAME_@TYPE_ID(
		ao::schema::cpp::CppDecodeRuntime& runtime,
		ao::schema::cpp::MutPtr ptr,
        uint16_t width,
        int64_t v) {
    ptr.as<@GENERATED_TYPE>() = (@GENERATED_TYPE)v;
}

ao::schema::cpp::EncodeTypeOps const @QNAME::encode = ao::schema::cpp::EncodeTypeOps{
	.@OP_NAME = &encode_@OP_NAME_@TYPE_ID,
};
ao::schema::cpp::DecodeTypeOps const @QNAME::decode = ao::schema::cpp::DecodeTypeOps{
	.@OP_NAME = &decode_@OP_NAME_@TYPE_ID,
};
)",
                                {
                                    {"@OP_NAME", "i64"},
                                    {"@TYPE_ID", std::to_string(typeId)},
                                    {"@QNAME", qname},
                                    {"@GENERATED_TYPE", generatedType},
                                });
}
