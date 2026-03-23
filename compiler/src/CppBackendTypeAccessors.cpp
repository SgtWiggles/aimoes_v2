#include "CppBackendHelpers.h"

#include <string>
#include <variant>

#include "ao/schema/IR.h"
#include "ao/utils/Overloaded.h"

using namespace ao;
using namespace ao::schema;

static std::string const optionalStringTemplate = R"(
static bool encodeOptionalHasValue(
 ao::schema::cpp::CppEncodeRuntime& runtime,
 ao::schema::cpp::AnyPtr ptr) {
 return ptr.as<@TYPE_NAME>().has_value();
}
static void encodeOptionalEnter(
 ao::schema::cpp::CppEncodeRuntime& runtime,
 ao::schema::cpp::AnyPtr ptr) {
}
static void encodeOptionalExit(
 ao::schema::cpp::CppEncodeRuntime& runtime,
 ao::schema::cpp::AnyPtr ptr) {
}
static void encodeOptionalEnterValue(
 ao::schema::cpp::CppEncodeRuntime& runtime,
 ao::schema::cpp::AnyPtr ptr) {
 auto& v = ptr.as<@TYPE_NAME>();
 if (!v.has_value()) {
 ao::schema::cpp::cppRuntimeFail(runtime, ao::pack::Error::BadData);
 return;
 }
 runtime.stack.emplace_back(ao::schema::cpp::EncodeFrame{
 .ops = &(@SUBTYPE_ACCESSOR::encode),
 .data = ao::schema::cpp::AnyPtr{(void*)&v.value()},
 });
}
static void encodeOptionalExitValue(ao::schema::cpp::CppEncodeRuntime& runtime, ao::schema::cpp::AnyPtr ptr) {
 runtime.stack.pop_back();
}

static void decodeOptionalEnter(
 ao::schema::cpp::CppDecodeRuntime& runtime,
 ao::schema::cpp::MutPtr ptr) {
 auto& data = ptr.as<@TYPE_NAME>();
 data.reset();
}
static void decodeOptionalExit(
 ao::schema::cpp::CppDecodeRuntime& runtime,
 ao::schema::cpp::MutPtr ptr) {
 // Do nothing
}
static void decodeOptionalSetPresent(
 ao::schema::cpp::CppDecodeRuntime& runtime,
 ao::schema::cpp::MutPtr ptr,
 bool present) {
 auto& data = ptr.as<@TYPE_NAME>();
 if (present) {
 data.emplace();
 } else {
 data.reset();
 }
}

static void decodeOptionalEnterValue(
 ao::schema::cpp::CppDecodeRuntime& runtime,
 ao::schema::cpp::MutPtr ptr) {
 auto& data = ptr.as<@TYPE_NAME>();
 if (!data.has_value()) {
 ao::schema::cpp::cppRuntimeFail(runtime, ao::pack::Error::BadData);
 return;
 }
 runtime.stack.emplace_back(ao::schema::cpp::DecodeFrame{
 .ops = &(@SUBTYPE_ACCESSOR::decode),
 .data = ao::schema::cpp::MutPtr{(void*)&data.value()},
 });
}
static void decodeOptionalExitValue(
 ao::schema::cpp::CppDecodeRuntime& runtime,
 ao::schema::cpp::MutPtr ptr) {
 runtime.stack.pop_back();
}

public:
static constexpr auto encode = ao::schema::cpp::EncodeTypeOps{
 .optionalHasValue = &encodeOptionalHasValue,
 .optionalEnter = &encodeOptionalEnter,
 .optionalExit = &encodeOptionalExit,
 .optionalEnterValue = &encodeOptionalEnterValue,
 .optionalExitValue = &encodeOptionalExitValue,
};


static constexpr auto decode = ao::schema::cpp::DecodeTypeOps{
 .optionalEnter = &decodeOptionalEnter,
 .optionalExit = &decodeOptionalExit,
 .optionalSetPresent = &decodeOptionalSetPresent,
 .optionalEnterValue = &decodeOptionalEnterValue,
 .optionalExitValue = &decodeOptionalExitValue,
};

)";

static void generateTypeAccessorOptional(CppCodeGenContext& ctx,
                                         std::stringstream& ss,
                                         size_t typeId,
                                         ir::Optional const& v) {
    auto subtypeId = ctx.generatedAccessors[v.type.idx].name.qualifiedName();
    auto const& typeName = ctx.generatedTypeNames[typeId];

    ss << replaceMany(optionalStringTemplate,
                      {
                          {"@TYPE_NAME", typeName.qualifiedName()},
                          {"@SUBTYPE_ACCESSOR", subtypeId},
                      });
}

static void generateTypeAccessorOneof(CppCodeGenContext& ctx,
                                      std::stringstream& ss,
                                      size_t typeId,
                                      ir::OneOf const& oneofDesc) {
    auto const& typeName = ctx.generatedTypeNames[typeId];

    // TODO abstract this armid into another lamba.
    // Maybe even abstract C++ into something more token based to
    // ensure we don't have mismatch braces
    std::string encodeOneofEnterArm = replaceMany(
        R"(
static void encodeOneofEnterArm(
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
        });
    enumerate(oneofDesc.arms, [&](size_t idx, IdFor<ir::Field> fieldId) {
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
static void decodeOneofIndex(
 ao::schema::cpp::CppDecodeRuntime& runtime,
 ao::schema::cpp::MutPtr ptr,
 uint32_t oneofId,
 uint32_t armId) {
 auto& data = ptr.as<@TYPE_NAME>();
 switch(armId) {
)",
                    {{"@TYPE_NAME", typeName.qualifiedName()}});

    enumerate(oneofDesc.arms, [&](size_t idx, IdFor<ir::Field> fieldId) {
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
static void decodeOneofEnterArm(
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

    enumerate(oneofDesc.arms, [&](size_t idx, IdFor<ir::Field> fieldId) {
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

    ss << replaceMany(R"(
static uint32_t encodeOneofIndex(
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
static void encodeOneofEnter(
 ao::schema::cpp::CppEncodeRuntime& runtime,
 ao::schema::cpp::AnyPtr ptr,
 uint32_t oneofId) {
 // do nothing
}
static void encodeOneofExit(
 ao::schema::cpp::CppEncodeRuntime& runtime,
 ao::schema::cpp::AnyPtr ptr) {
 // do nothing
}

@ENTER_ENCODE_ARM

static void encodeOneofExitArm(
 ao::schema::cpp::CppEncodeRuntime& runtime,
 ao::schema::cpp::AnyPtr ptr) {
 runtime.stack.pop_back();
}


static void decodeOneofEnter(
 ao::schema::cpp::CppDecodeRuntime& runtime,
 ao::schema::cpp::MutPtr ptr,
 uint32_t oneofId) {
 
}
static void decodeOneofExit(
 ao::schema::cpp::CppDecodeRuntime& runtime,
 ao::schema::cpp::MutPtr ptr) {

}

@DECODE_INDEX

@DECODE_ENTER_ARM

static void decodeOneofExitArm(
 ao::schema::cpp::CppDecodeRuntime& runtime,
 ao::schema::cpp::MutPtr ptr) {
 runtime.stack.pop_back();
}


public:
static constexpr auto encode = ao::schema::cpp::EncodeTypeOps{
 .oneofIndex = &encodeOneofIndex,
 .oneofEnter = &encodeOneofEnter,
 .oneofExit = &encodeOneofEnter,
 .oneofEnterArm = &encodeOneofEnterArm,
 .oneofExitArm = &encodeOneofExitArm,
};
static constexpr auto decode = ao::schema::cpp::DecodeTypeOps{
	.oneofEnter = &decodeOneofEnter,
	.oneofExit = &decodeOneofExit,
	.oneofIndex = &decodeOneofIndex,
	.oneofEnterArm = &decodeOneofEnterArm,
	.oneofEnterArm = &decodeOneofExitArm,
};
)",
                      {
                          {"@TYPE_NAME", typeName.qualifiedName()},
                          {"@ENCODE_ENTER_ARM", encodeOneofEnterArm},
                          {"@DECODE_INDEX", decodeOneofIndex},
                          {"@DECODE_ENTER_ARM", decodeOneofEnterArm},
                      });
}

static std::string generateScalarAccessor(std::string_view internalType,
                                          std::string_view generatedType,
                                          std::string_view opName,
                                          bool needsWidth) {
    return replaceMany(
        R"(
static @INTERNAL_TYPE encode_@OP_NAME(ao::schema::cpp::CppEncodeRuntime& runtime,
					ao::schema::cpp::AnyPtr ptr @NEEDS_WIDTH) {
 return (@INTERNAL_TYPE)ptr.as<@GENERATED_TYPE>();
}

static void decode_@OP_NAME(
		ao::schema::cpp::CppDecodeRuntime& runtime,
		ao::schema::cpp::MutPtr ptr @NEEDS_WIDTH ,@INTERNAL_TYPE v) {
 ptr.as<@GENERATED_TYPE>() = (@GENERATED_TYPE)v;
}

public:
static constexpr auto encode = ao::schema::cpp::EncodeTypeOps {
	.@OP_NAME = &encode_@OP_NAME,
};
static constexpr  auto decode = ao::schema::cpp::DecodeTypeOps{
	.@OP_NAME = &decode_@OP_NAME,
};
)",
        {
            {"@INTERNAL_TYPE", internalType},
            {"@GENERATED_TYPE", generatedType},
            {"@OP_NAME", opName},
            {"@NEEDS_WIDTH", needsWidth ? ", uint16_t width" : ""},
        });
}

static void generateTypeAccessorScalar(CppCodeGenContext& ctx,
                                       std::stringstream& ss,
                                       size_t typeId,
                                       ir::Scalar const& v) {
    auto const& typeName = ctx.generatedTypeNames[typeId];

    switch (v.kind) {
        case ir::Scalar::BOOL: {
            ss << generateScalarAccessor("bool", typeName.name, "boolean",
                                         false);
        } break;
        case ir::Scalar::INT: {
            ss << generateScalarAccessor("int64_t", typeName.name, "i64", true);
        } break;
        case ir::Scalar::UINT: {
            ss << generateScalarAccessor("uint64_t", typeName.name, "u64",
                                         true);
        } break;
        case ir::Scalar::F32: {
            ss << generateScalarAccessor("float", typeName.name, "f32", false);
        } break;
        case ir::Scalar::F64: {
            ss << generateScalarAccessor("double", typeName.name, "f64", false);
        } break;
        case ir::Scalar::CHAR: {
            ss << generateScalarAccessor("uint64_t", typeName.name, "u64",
                                         true);
        } break;
        case ir::Scalar::BYTE: {
            ss << generateScalarAccessor("uint64_t", typeName.name, "u64",
                                         true);
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

static void generateTypeAccessorArray(CppCodeGenContext& ctx,
                                      std::stringstream& ss,
                                      size_t typeId,
                                      ir::Array const& v) {
    auto subtypeAccessor =
        ctx.generatedAccessors[v.type.idx].name.qualifiedName();
    auto const& typeName = ctx.generatedTypeNames[typeId];
    ss << replaceMany(R"(
static void encodeArrayEnter(
 ao::schema::cpp::CppEncodeRuntime& runtime,
 ao::schema::cpp::AnyPtr ptr,
 uint32_t typeId) {
 // Do nothing
}
static void encodeArrayExit(
 ao::schema::cpp::CppEncodeRuntime& runtime,
 ao::schema::cpp::AnyPtr ptr) {
 // Do nothing
}
static uint32_t encodeArrayLen(
 ao::schema::cpp::CppEncodeRuntime& runtime,
 ao::schema::cpp::AnyPtr ptr) {
 auto const& data = ptr.as<@TYPE_NAME>();
 return data.size();
}
static void encodeArrayEnterElem(
 ao::schema::cpp::CppEncodeRuntime& runtime,
 ao::schema::cpp::AnyPtr ptr,
 uint32_t i) {
 auto const& data = ptr.as<@TYPE_NAME>();
 if (i >= data.size()) {
 ao::schema::cpp::cppRuntimeFail(runtime, ao::pack::Error::BadData);
 return;
 }
 runtime.stack.emplace_back(ao::schema::cpp::EncodeFrame{
 .ops = &(@SUBTYPE_ACCESSOR::encode),
 .data = AnyPtr{(void const*)&data[i]},
 });
}
static void encodeArrayExitElem(
 ao::schema::cpp::CppEncodeRuntime& runtime,
 ao::schema::cpp::AnyPtr ptr) {
 if (runtime.stack.empty()) {
 ao::schema::cpp::cppRuntimeFail(runtime, ao::pack::Error::BadData)
 return;
 }
 runtime.stack.pop_back();
}


static void decodeArrayEnter(CppDecodeRuntime& runtime,
				 MutPtr ptr,
				 uint32_t typeId) {
 // Do nothing
}
static void decodeArrayExit(CppDecodeRuntime& runtime, MutPtr ptr) {
 // Do nothing
}
static void decodeArrayPrepare(CppDecodeRuntime& runtime,
				 MutPtr ptr,
				 uint32_t len) {
 auto& data = ptr.as<@TYPE_NAME>();
 data.resize(len);
}
static void decodeArrayEnterElem(CppDecodeRuntime& runtime,
				 MutPtr ptr,
				 uint32_t i) {
 auto& data = ptr.as<@TYPE_NAME>();
 if (i >= data.size()) {
 ao::schema::cpp::cppRuntimeFail(runtime, ao::pack::Error::BadData);
 return;
 }
 runtime.stack.emplace_back(ao::schema::cpp::DecodeFrame{
 .ops = &(@SUBTYPE_ACCESSOR::decode),
 .data = MutPtr{(void*)&data[i]},
 });
}
static void decodeArrayExitElem(CppDecodeRuntime& runtime, MutPtr ptr) {
 if (runtime.stack.empty()) {
 ao::schema::cpp::cppRuntimeFail(runtime, ao::pack::Error::BadData)
 return;
 }
 runtime.stack.pop_back();
}

public:
static constexpr auto encode = ao::schema::cpp::EncodeTypeOps{
 .arrayEnter = &encodeArrayEnter,
 .arrayExit = &encodeArrayExit,
 .arrayLen = &encodeArrayLen,
 .arrayEnterElem = &encodeArrayEnterElem,
 .arrayExitElem = &encodeArrayExitElem, 
};

static constexpr auto decode = ao::schema::cpp::DecodeTypeOps{
 .arrayEnter = &decodeArrayEnter,
 .arrayExit = &decodeArrayExit,
 .arrayPrepare = &decodeArrayPrepare,
 .arayEnterElem = &decodeArrayEnterElem,
 .arrayExitElem = &decodeArrayExitElem
};
)",
                      {
                          {"@TYPE_NAME", typeName.qualifiedName()},
                          {"@SUBTYPE_ACCESSOR", subtypeAccessor},
                      });
}

static const std::string_view encodeRuntime =
    "ao::schema::cpp::CppEncodeRuntime";
static const std::string_view anyPtr = "ao::schema::cpp::AnyPtr";
static const std::string_view decodeRuntime =
    "ao::schema::cpp::CppDecodeRuntime";
static const std::string_view mutPtr = "ao::schema::cpp::MutPtr";

template <class... T>
std::string funcSig(std::string_view sig,
                    std::string_view name,
                    T const&... args) {
    size_t idx = 0;
    std::string ret = std::format("static {} {}(", sig, name);
    ((idx += 1, ret += std::vformat(idx == 1 ? "{}" : ", {}",
                                    std::make_format_args(args))),
     ...);
    ret += ")";
    return ret;
}

static void encodeFieldBegin(CppCodeGenContext& ctx,
                             std::stringstream& ss,
                             size_t typeId,
                             IdFor<ir::Message> v) {
    auto signature = funcSig("void", "encodeFieldBegin",
                             std::string{encodeRuntime} + "& runtime",
                             std::string{anyPtr} + " ptr", "uint32_t fieldId");
    auto const& typeName = ctx.generatedTypeNames[typeId];
    ss << replaceMany(R"(@FUNC_SIG {
auto const& data = ptr.as<@TYPE_NAME>();
switch (fieldId) {
)",
                      {
                          {"@FUNC_SIG", signature},
                          {"@TYPE_NAME", typeName.qualifiedName()},
                      });
    enumerate(ctx.ir.messages[v.idx].fields,
              [&](size_t fieldId, IdFor<ir::Field> const& globalFieldId) {
                  auto const& fieldDesc = ctx.ir.fields[globalFieldId.idx];
                  ss << replaceMany(
                      R"(
case @FIELD_ID: {
 auto fieldPtr = &data.@FIELD_NAME;
	runtime.stack.emplace_back(ao::schema::cpp::EncodeFrame{
		.ops = &@SUBTYPE_ACCESSOR::encode,
		.data = {fieldPtr},
	});
} break;

)",
                      {
                          {"@FIELD_ID", std::to_string(globalFieldId.idx)},
                          {"@FIELD_NAME", ctx.ir.strings[fieldDesc.name.idx]},
                          {
                              "@SUBTYPE_ACCESSOR",
                              ctx.generatedAccessors[fieldDesc.type.idx]
                                  .name.qualifiedName(),
                          },
                      });
              });
    ss << R"(
default: 
	ao::schema::cpp::cppRuntimeFail(runtime, ao::pack::Error::BadData);
	return;
}
}
)";
}
static void decodeFieldBegin(CppCodeGenContext& ctx,
                             std::stringstream& ss,
                             size_t typeId,
                             IdFor<ir::Message> v) {
    ss << funcSig("void", "decodeFieldBegin",
                  std::string{decodeRuntime} + "& runtime",
                  std::string{mutPtr} + " ptr", "uint32_t fieldId")
       << " {\n";
    ss << std::format("auto& data = ptr.as<{}>();\n",
                      ctx.generatedTypeNames[typeId].qualifiedName());
    ss << "switch (fieldId) {\n";

    auto const& msgDesc = ctx.ir.messages[v.idx];
    enumerate(msgDesc.fields, [&](size_t idx, IdFor<ir::Field> fieldId) {
        auto const& fieldDesc = ctx.ir.fields[fieldId.idx];
        ss << replaceMany(
            R"(
case @FIELD_ID: {
    auto* fieldPtr = &data.@FIELD_NAME;
	runtime.stack.emplace_back(ao::schema::cpp::DecodeFrame{
		.ops = &(@SUBTYPE_ACCESSOR::decode),
		.data = {fieldPtr},
	});
 
} break;
)",
            {
                {"@FIELD_ID", std::to_string(fieldId.idx)},
                {
                    "@SUBTYPE_ACCESSOR",
                    ctx.generatedAccessors[fieldDesc.type.idx]
                        .name.qualifiedName(),
                },
                {"@FIELD_NAME", ctx.ir.strings[fieldDesc.name.idx]},
            });
    });
    ss << R"(
default: 
	ao::schema::cpp::cppRuntimeFail(runtime, ao::pack::Error::BadData);
	return;
}
}
)";
}

static void generateTypeAccessorMessage(CppCodeGenContext& ctx,
                                        std::stringstream& ss,
                                        size_t typeId,
                                        IdFor<ir::Message> v) {
    auto const& msgDesc = ctx.ir.messages[v.idx];
    auto const& typeName = ctx.generatedTypeNames[typeId];
    ss << funcSig("void", "encodeMsgBegin",
                  std::string{encodeRuntime} + "& runtime",
                  std::string{anyPtr} + " ptr", "uint32_t msgId")
       << " {}\n";
    ss << funcSig("void", "encodeMsgEnd",
                  std::string{encodeRuntime} + "& runtime",
                  std::string{anyPtr} + " ptr")
       << " {}\n";
    ss << funcSig("void", "decodeMsgBegin",
                  std::string{decodeRuntime} + "& runtime",
                  std::string{mutPtr} + " ptr", "uint32_t msgId")
       << " {}\n";
    ss << funcSig("void", "decodeMsgEnd",
                  std::string{decodeRuntime} + "& runtime",
                  std::string{mutPtr} + " ptr")
       << " {}\n";

    encodeFieldBegin(ctx, ss, typeId, v);
    ss << funcSig("void", "encodeFieldEnd",
                  std::string{encodeRuntime} + "& runtime",
                  std::string{anyPtr} + " ptr")
       << "{ runtime.stack.pop_back(); }\n";

    decodeFieldBegin(ctx, ss, typeId, v);
    ss << funcSig("void", "decodeFieldEnd",
                  std::string{decodeRuntime} + "& runtime",
                  std::string{mutPtr} + " ptr")
       << "{ runtime.stack.pop_back(); }\n";

    ss << R"(
public:
static constexpr auto encode = ao::schema::cpp::EncodeTypeOps{
 .msgBegin = &encodeMsgBegin,
 .msgEnd = &encodeMsgEnd,
 .fieldBegin = &encodeFieldBegin,
 .fieldEnd = &encodeFieldEnd,
};
static constexpr auto decode = ao::schema::cpp::DecodeTypeOps{
 .msgBegin = &decodeMsgBegin,
 .msgEnd = &decodeMsgEnd,
 .fieldBegin = &decodeFieldBegin,
 .fieldEnd = &decodeFieldEnd,
};
)";
}

static std::string generateTypeAccessorsImpl(CppCodeGenContext& ctx,
                                             size_t typeId,
                                             ir::Type const& type) {
    std::stringstream ss;
    auto const& typeName = ctx.generatedTypeNames[typeId];

    ss << "struct " << ctx.generatedAccessors[typeId].name.name << " {\n";
    std::visit(Overloaded{
                   [&](ir::Scalar const& v) {
                       generateTypeAccessorScalar(ctx, ss, typeId, v);
                   },
                   [&](ir::Array const& v) {
                       generateTypeAccessorArray(ctx, ss, typeId, v);
                   },
                   [&](ir::Optional const& v) {
                       generateTypeAccessorOptional(ctx, ss, typeId, v);
                   },
                   [&](IdFor<ir::OneOf> const& v) {
                       generateTypeAccessorOneof(ctx, ss, typeId,
                                                 ctx.ir.oneOfs[v.idx]);
                   },
                   [&](IdFor<ir::Message> const& v) {
                       generateTypeAccessorMessage(ctx, ss, typeId, v);
                   },
               },
               type.payload);

    ss << "};\n";
    return ss.str();
}

std::string generateTypeAccessors(CppCodeGenContext& ctx,
                                  size_t typeId,
                                  ir::Type const& type) {
    return generateTypeAccessorsImpl(ctx, typeId, type);
}
