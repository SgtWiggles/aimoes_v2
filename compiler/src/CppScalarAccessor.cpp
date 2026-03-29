#include "CppTypeAccessor.h"
using namespace ao;
using namespace ao::schema;

static std::string generateScalarAccessor(std::string_view internalType,
                                          std::string_view generatedType,
                                          std::string_view opName,
                                          size_t typeId,
                                          std::string qname,
                                          bool needsWidth) {
    return replaceMany(
        R"(
@INTERNAL_TYPE encode_@OP_NAME_@TYPE_ID(ao::schema::cpp::CppEncodeRuntime& runtime,
					ao::schema::cpp::AnyPtr ptr @NEEDS_WIDTH) {
 return (@INTERNAL_TYPE)ptr.as<@GENERATED_TYPE>();
}

void decode_@OP_NAME_@TYPE_ID(
		ao::schema::cpp::CppDecodeRuntime& runtime,
		ao::schema::cpp::MutPtr ptr @NEEDS_WIDTH ,@INTERNAL_TYPE v) {
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
            {"@INTERNAL_TYPE", internalType},
            {"@GENERATED_TYPE", generatedType},
            {"@OP_NAME", opName},
            {"@NEEDS_WIDTH", needsWidth ? ", uint16_t width" : ""},
            {"@TYPE_ID", std::to_string(typeId)},
            {"@QNAME", qname},
        });
}

static void generateTypeAccessorScalar(CppCodeGenContext& ctx,
                                       std::stringstream& ss,
                                       size_t typeId,
                                       ir::Scalar const& v) {
    auto const& typeName = ctx.generatedTypeNames[typeId];
    auto qname = ctx.generatedAccessors[typeId].name.qualifiedName();

    switch (v.kind) {
        case ir::Scalar::BOOL: {
            ss << generateScalarAccessor("bool", typeName.name, "boolean",
                                         typeId, qname, false);
        } break;
        case ir::Scalar::INT: {
            ss << generateScalarAccessor("int64_t", typeName.name, "i64",
                                         typeId, qname, true);
        } break;
        case ir::Scalar::UINT: {
            ss << generateScalarAccessor("uint64_t", typeName.name, "u64",
                                         typeId, qname, true);
        } break;
        case ir::Scalar::F32: {
            ss << generateScalarAccessor("float", typeName.name, "f32", typeId,
                                         qname, false);
        } break;
        case ir::Scalar::F64: {
            ss << generateScalarAccessor("double", typeName.name, "f64", typeId,
                                         qname, false);
        } break;
        case ir::Scalar::CHAR: {
            ss << generateScalarAccessor("uint64_t", typeName.name, "u64",
                                         typeId, qname, true);
        } break;
        case ir::Scalar::BYTE: {
            ss << generateScalarAccessor("uint64_t", typeName.name, "u64",
                                         typeId, qname, true);
        } break;
        default:
            ctx.errs.fail({
                .code = ao::schema::ErrorCode::INTERNAL,
                .message = std::format("Cpp unsupported type: {}", (int)v.kind),
                .loc = {},
            });
            break;
    }
}

void generateTypeAccessorScalar(CppCodeGenContext& ctx,
                                size_t typeId,
                                ao::schema::ir::Scalar const& scalar) {
    std::stringstream ss;
    generateTypeAccessorScalar(ctx, ss, typeId, scalar);
    ctx.generatedAccessors[typeId].impl = ss.str();
}
