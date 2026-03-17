#include "ao/schema/CppBackend.h"

#include "ao/schema/CppAdapter.h"

#include <sstream>

#include "ao/utils/Array.h"
#include "ao/utils/Overloaded.h"

namespace ao::schema::cpp {
struct CppCodeGenContext {
    ir::IR const& ir;
    ErrorContext& errs;

    std::vector<std::string> generatedTypeNames;
    std::vector<std::string> generatedTypeDecls;
    std::vector<std::string> generatedTypeDefs;
    std::vector<std::string> generatedMessages;
};

uint8_t getCppBitWidth(CppCodeGenContext& ctx, uint64_t width) {
    // default to uint64_t if no width specified, this is
    // consistent with the codec table generation
    if (width == 0)
        return 64;

    static constexpr auto items = makeArray<int>(8, 16, 32, 64);
    for (auto item : items) {
        if (width <= item)
            return static_cast<uint8_t>(item);
    }

    ctx.errs.fail({
        .code = ErrorCode::INTERNAL,
        .message = std::format("Unsupported bit width for C++: {}", width),
        .loc = {},
    });
    return 0;
}

static std::string generateTypeName(CppCodeGenContext& ctx,
                                    size_t typeId,
                                    ir::Type const& type) {
    return std::visit(
        Overloaded{
            [&ctx](ir::Scalar const& v) -> std::string {
                auto cppWidth = getCppBitWidth(ctx, v.width);
                switch (v.kind) {
                    case ir::Scalar::BOOL:
                        return "bool";
                    case ir::Scalar::INT:
                        return std::format("int{}_t", cppWidth);
                    case ir::Scalar::UINT:
                        return std::format("uint{}_t", cppWidth);
                    case ir::Scalar::F32:
                        return "float";
                    case ir::Scalar::F64:
                        return "double";
                    case ir::Scalar::CHAR:
                        return "char";
                    case ir::Scalar::BYTE:
                        return "std::byte";
                    default:
                        ctx.errs.fail({
                            .code = ErrorCode::INTERNAL,
                            .message = std::format(
                                "Unknown C++ scalar type: {}", (int)v.kind),
                            .loc = {},
                        });
                        break;
                }

                return "<error>";
            },
            [typeId, &ctx](ir::Array const& v) -> std::string {
                auto scalar =
                    std::get_if<ir::Scalar>(&ctx.ir.types[v.type.idx].payload);
                if (scalar) {
                    if (scalar->kind == ir::Scalar::CHAR)
                        return "std::string";
                    if (scalar->kind == ir::Scalar::BYTE)
                        return "std::vector<std::byte>";
                }
                return std::format("Type_{}_Arr", typeId);
            },
            [typeId](ir::Optional const& v) -> std::string {
                return std::format("Type_{}_Opt", typeId);
            },
            [typeId](IdFor<ir::OneOf> const& v) -> std::string {
                return std::format("Type_{}_Oneof", typeId);
            },
            [typeId](IdFor<ir::Message> const& v) -> std::string {
                return std::format("Type_{}_Msg", typeId);
            },
        },
        type.payload);
}

static std::optional<std::string> generateTypeDecl(CppCodeGenContext& ctx,
                                                   size_t typeId,
                                                   ir::Type const& type) {
    return std::visit(
        Overloaded{
            [&ctx](ir::Scalar const& v) -> std::optional<std::string> {
                return {};
            },
            [typeId, &ctx](ir::Array const& v) -> std::optional<std::string> {
                return {};
            },
            [typeId](ir::Optional const& v) -> std::optional<std::string> {
                return {};
            },
            [typeId](IdFor<ir::OneOf> const& v) -> std::optional<std::string> {
                return {};
            },
            [&ctx, typeId](
                IdFor<ir::Message> const& v) -> std::optional<std::string> {
                return std::format("struct {};",
                                   ctx.generatedTypeNames[typeId]);
            },
        },
        type.payload);
}

static std::optional<std::string> generateTypeDef(CppCodeGenContext& ctx,
                                                  size_t typeId,
                                                  ir::Type const& type) {
    return std::visit(
        Overloaded{
            [&ctx](ir::Scalar const& v) -> std::optional<std::string> {
                return {};
            },
            [&ctx, typeId](ir::Array const& v) -> std::optional<std::string> {
                auto scalar =
                    std::get_if<ir::Scalar>(&ctx.ir.types[v.type.idx].payload);
                if (scalar) {
                    if (scalar->kind == ir::Scalar::CHAR)
                        return {};
                    if (scalar->kind == ir::Scalar::BYTE)
                        return {};
                }
                return std::format("using {} = std::vector<{}>",
                                   ctx.generatedTypeNames[typeId],
                                   ctx.generatedTypeNames[v.type.idx]);
            },
            [&ctx,
             typeId](ir::Optional const& v) -> std::optional<std::string> {
                return std::format("using {} = std::optional<{}>",
                                   ctx.generatedTypeNames[typeId],
                                   ctx.generatedTypeNames[v.type.idx]);
            },
            [&ctx,
             typeId](IdFor<ir::OneOf> const& v) -> std::optional<std::string> {
                std::stringstream ss;
                ss << std::format("using {} = std::variant<std::monostate",
                                  ctx.generatedTypeNames[typeId]);
                for (auto const& armField : ctx.ir.oneOfs[v.idx].arms) {
                    ss << ", ";
                    auto const& arm = ctx.ir.fields[armField.idx];
                    ss << "\n    " << ctx.generatedTypeNames[arm.type.idx];
                }
                ss << "\n>";
                return ss.str();
            },
            [&ctx, typeId](
                IdFor<ir::Message> const& v) -> std::optional<std::string> {
                auto const& msg = ctx.ir.messages[v.idx];
                std::stringstream ss;
                ss << std::format("struct Type_{}_Msg {{\n", typeId);
                for (auto const& fieldId : msg.fields) {
                    auto const& field = ctx.ir.fields[fieldId.idx];
                    auto const& fieldName = ctx.ir.strings[field.name.idx];
                    auto const& fieldTypeName =
                        ctx.generatedTypeNames[field.type.idx];
                    ss << std::format("    {} {};\n", fieldTypeName, fieldName);
                }
                ss << "};";
                return ss.str();
            },
        },
        type.payload);
}

std::vector<std::string_view> parsePackageName(std::string_view str) {
    std::vector<std::string_view> output;
    size_t lastWindow = 0;
    for (size_t idx = 0; idx < str.size(); ++idx) {
        if (str[idx] != '.')
            continue;
        auto count = idx - lastWindow;
        output.emplace_back(str.substr(lastWindow, count));
        lastWindow = idx + 1;
    }
    if (lastWindow < str.size()) {
        auto finalName = str.substr(lastWindow, str.size() - lastWindow);
        output.emplace_back(finalName);
    }
    return output;
}

static std::optional<std::string> generateNamespaceForwarding(
    CppCodeGenContext& ctx,
    size_t typeId,
    IdFor<ir::Message> const& msgIdx) {
    std::stringstream ss;
    auto const& msg = ctx.ir.messages[msgIdx.idx];
    auto& name = ctx.ir.strings[msg.name.idx];
    auto packageName = parsePackageName(name);
    if (packageName.empty())
        return {};

    auto namespaceName = std::string{};
    for (size_t idx = 0; idx < packageName.size() - 1; ++idx) {
        if (!namespaceName.empty())
            namespaceName += "::";
        namespaceName += packageName[idx];
    }

    if (!namespaceName.empty()) {
        ss << "namespace " << namespaceName << " {\n";
    }

    ss << "using " << packageName.back()
       << " = aosl_detail::" << ctx.generatedTypeNames[typeId] << "\n";

    if (!namespaceName.empty()) {
        ss << "}\n";
    }

    return ss.str();
}

static std::string generateTypeAccessorsOptional(CppCodeGenContext& ctx,
                                                 std::stringstream& ss,
                                                 size_t typeId,
                                                 ir::Optional const& v) {
    auto subtypeAccessor = std::format("CppAccessor<{}>", v.type.idx);
    auto const& typeName = ctx.generatedTypeNames[typeId];
    ss << std::format(R"(
static bool encodeOptionalHasValue(
    ao::schema::cpp::CppEncodeRuntime& runtime,
    ao::schema::cpp::AnyPtr ptr) {
    return ptr.as<{0}>().has_value();
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
    auto& v = ptr.as<{0}>();
    if (!v.has_value()) {
        ao::schema::cpp::cppRuntimeFail(runtime, ao::pack::Error::BadData);
        return;
    }
    runtime.encodeStack.emplace_back({
        .kind = ao::schema::cpp::FrameKind::OptionalValue,
        .ops = &({1}::encode),
        .data = AnyPtr{(void*)&v.value()},
    });
}
static void encodeOptionalExitValue(ao::schema::cpp::CppEncodeRuntime& runtime, ao::schema::cpp::AnyPtr ptr) {
    runtime.encodeStack.pop_back();
}
static auto encode = ao::schema::cpp::EncodeTypeOps{
    .optionalHasValue = &encodeOptionalHasValue,
    .optionalEnter = &encodeOptionalEnter,
    .optionalExit = &encodeOptionalExit,
    .optionalEnterValue = &encodeOptionalEnterValue,
    .optionalExitValue = &encodeOptionalExitValue,
};


static void decodeOptionalEnter(
    ao::schema::cpp::CppDecodeRuntime& runtime,
    ao::schema::cpp::MutPtr ptr) {
    auto& data = ptr.as<{0}>();
    data.clear();
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
    auto& data = ptr.as<{0}>();
    if (present) {
        data.emplace();
    } else {
        data.clear();
    }
}

static void decodeOptionalEnterValue(
    ao::schema::cpp::CppDecodeRuntime& runtime,
    ao::schema::cpp::MutPtr ptr) {
    auto& data = ptr.as<{0}>();
    if (!data.has_value()) {
        ao::schema::cpp::cppRuntimeFail(runtime, ao::pack::Error::BadData)
        return;
    }
    runtime.encodeStack.emplace_back({
        .kind = ao::schema::cpp::FrameKind::OptionalValue,
        .ops = &({1}::decode),
        .data = MutPtr{(void*)&data.value()},
    });
}
static void decodeOptionalExitValue(
    ao::schema::cpp::CppDecodeRuntime& runtime,
    ao::schema::cpp::MutPtr ptr) {
    runtime.encodeStack.pop_back();
}
static auto decode = ao::schema::cpp::DecodeTypeOps{
    .optionalEnter = &decodeOptionalEnter,
    .optionalExit = &decodeOptionalExit,
    .optionalSetPresent = &decodeOptionalSetPresent,
    .optionalEnterValue = &decodeOptionalEnterValue,
    .optionalExitValue = &decodeOptionalExitValue,
};
)",
                      typeName, subtypeAccessor);
}

static std::string generateTypeAccessorsOneof(CppCodeGenContext& ctx,
                                              std::stringstream& ss,
                                              size_t typeId,
                                              ir::OneOf const& oneofDesc) {
    auto const& typeName = ctx.generatedTypeNames[typeId];

    // TODO abstract this armid into another lamba.
    // Maybe even abstract C++ into something more token based to
    // ensure we don't have mismatch braces
    std::string encodeOneofEnterArm =
        R"(
static void encodeOneofEnterArm(
    ao::schema::cpp::CppEncodeRuntime& runtime,
    ao::schema::cpp::AnyPtr ptr,
	uint32_t oneofId,
	uint32_t armId) {
    auto const& data = ptr.as<{0}>();
    if (data.index() != armId + 1) {
        ao::schema::cpp::cppRuntimeFail(runtime, ao::pack::Error::BadData)
        return;
    }

    switch (armId) {
)";
    enumerate(oneofDesc.arms, [&](size_t idx, IdFor<ir::Field> fieldId) {
        auto const& fieldDesc = ctx.ir.fields[fieldId.idx];
        encodeOneofEnterArm += std::format(R"(
		case {0}: {
			auto encodePtr = &CppAccessor<{1}>::encode;
			auto dataPtr = (void const*)(&std::get<{0} + 1>(data));
			runtime.encodeStack.emplace_back({
				.kind = ao::schema::cpp::FrameKind::OneofArm,
				.ops = encodePtr,
				.data = ao::schema::cpp::AnyPtr{dataPtr},
			});
		} break;
)",
                                           idx, fieldDesc.type.idx);
    });

    encodeOneofEnterArm += R"(
		default: {
			ao::schema::cpp::cppRuntimeFail(runtime, ao::pack::Error::BadData)
			return;
		} break;
    }
})";

    std::string decodeOneofIndex = std::format(R"(
static void decodeOneofIndex(
    ao::schema::cpp::CppDecodeRuntime& runtime,
    ao::schema::cpp::MutPtr ptr,
    uint32_t oneofId,
    uint32_t armId) {
    auto& data = ptr.as<{0}>();
    switch(armId) {
)",
                                               typeName);

    enumerate(oneofDesc.arms, [&](size_t idx, IdFor<ir::Field> fieldId) {
        auto const& fieldDesc = ctx.ir.fields[fieldId.idx];
        encodeOneofEnterArm += std::format(R"(
		case {0}: {
			auto ops = &CppAccessor<{1}>::decode;
            data.emplace<{0} + 1>();
		} break;
)",
                                           idx, fieldDesc.type.idx);
    });
    decodeOneofIndex += R"(
		default: {
			ao::schema::cpp::cppRuntimeFail(runtime, ao::pack::Error::BadData)
			return;
		} break;
    }
}
)";
    std::string decodeOneofEnterArm = std::format(R"(
static void decodeOneofEnterArm(
    ao::schema::cpp::CppDecodeRuntime& runtime,
	ao::schema::cpp::MutPtr ptr,
	uint32_t oneofId,
	uint32_t armId) {
    auto& data = ptr.as<{0}>();
    switch(armId) {
)",
                                                  typeName);

    enumerate(oneofDesc.arms, [&](size_t idx, IdFor<ir::Field> fieldId) {
        auto const& fieldDesc = ctx.ir.fields[fieldId.idx];
        decodeOneofEnterArm += std::format(R"(
		case {0}: {
			auto ops = &CppAccessor<{1}>::decode;
            auto value = std::get_if<{0} + 1>(&data);
			runtime.encodeStack.emplace_back({
				.kind = ao::schema::cpp::FrameKind::OneofArm,
				.ops = ops,
				.data = ao::schema::cpp::MutPtr{(void*)value},
			});
		} break;
)",
                                           idx, fieldDesc.type.idx);
    });

    decodeOneofEnterArm += R"(
		default: {
			ao::schema::cpp::cppRuntimeFail(runtime, ao::pack::Error::BadData)
			return;
		} break;
    }
}
)";

    ss << std::format(R"(
static uint32_t encodeOneofIndex(
    ao::schema::cpp::CppEncodeRuntime& runtime,
    ao::schema::cpp::AnyPtr ptr,
    uint32_t oneofId,
    uint32_t width) {
    auto& data = ptr.as<{0}>();
    auto idx = data.index();
    if (idx == 0)
        return std::numeric_limits<uint32_t>::max();
    return idx - 1;
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

{1}

static void encodeOneofExitArm(
    ao::schema::cpp::CppEncodeRuntime& runtime,
    ao::schema::cpp::AnyPtr ptr) {
    runtime.encodeStack.pop_back();
}

static auto encode = ao::schema::cpp::EncodeTypeOps{
    .oneofIndex = &encodeOneofIndex,
    .oneofEnter = &encodeOneofEnter,
    .oneofExit  = &encodeOneofEnter,
    .oneofEnterArm = &encodeOneofEnterArm,
    .oneofExitArm = &encodeOneofExitArm,
};

static void decodeOneofEnter(
    ao::schema::cpp::CppDecodeRuntime& runtime,
    ao::dschema::cpp::MutPtr ptr,
    uint32_t oneofId) {
    
}
static void decodeOneofExit(
    ao::schema::cpp::CppDecodeRuntime& runtime,
    ao::schema::cpp::MutPtr ptr) {

}

{2}

{3}

static void decodeOneofExitArm(
    ao::schema::cpp::CppDecodeRuntime& runtime,
    ao::schema::cpp::MutPtr ptr) {
    runtime.encodeStack.pop_back();
}
static auto decode = ao::schema::cpp::DecodeTypeOps{
	.oneofEnter = &decodeOneofEnter,
	.oneofExit = &decodeOneofExit,
	.oneofIndex = &decodeOneofIndex,
	.oneofEnterArm = &decodeOneofEnterArm,
	.oneofEnterArm = &decodeOneofExitArm,
};
)",
                      typeName, encodeOneofEnterArm, decodeOneofIndex,
                      decodeOneofEnterArm);
}

static std::string generateScalarAccessor(std::string_view internalType,
                                          std::string_view generatedType,
                                          std::string_view opName,
                                          bool needsWidth) {
    return std::format(R"(
static {0} encode_{2}(ao::schema::cpp::CppEncodeRuntime& runtime,
		ao::schema::cpp::AnyPtr ptr {3}) {
    return ({0})ptr.as<{1}>();
}

static void decode_{2}(
	ao::schema::cpp::CppEncodeRuntime& runtime,
	ao::schema::cpp::AnyPtr ptr {3} ,{0} v) {
    ptr.as<{1}>() = ({1})v;
}

static ao::schema::cpp::EncodeTypeOps encode{
	.{2} = &encode_{2},
};
static ao::schema::cpp::DecodeTypeOps decode{
	.{2} = &decode_{2},
};
)",
                       internalType, generatedType, opName,
                       needsWidth ? ", uint16_t width" : "");
}

static void generateTypeAccessorScalar(CppCodeGenContext& ctx,
                                       std::stringstream& ss,
                                       size_t typeId,
                                       ir::Scalar const& v) {
    auto const& typeName = ctx.generatedTypeNames[typeId];

    switch (v.kind) {
        case ir::Scalar::BOOL: {
            ss << generateScalarAccessor("bool", typeName, "boolean", false);
        } break;
        case ir::Scalar::INT: {
            ss << generateScalarAccessor("int64_t", typeName, "i64", true);
        } break;
        case ir::Scalar::UINT: {
            ss << generateScalarAccessor("uint64_t", typeName, "u64", true);
        } break;
        case ir::Scalar::F32: {
            ss << generateScalarAccessor("float", typeName, "f32", false);
        } break;
        case ir::Scalar::F64: {
            ss << generateScalarAccessor("double", typeName, "f64", false);
        } break;
        case ir::Scalar::CHAR: {
            ss << generateScalarAccessor("uint64_t", typeName, "u64", true);
        } break;
        case ir::Scalar::BYTE: {
            ss << generateScalarAccessor("uint64_t", typeName, "u64", true);
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
    auto subtypeAccessor = std::format("CppAccessor<{}>", v.type.idx);
    auto const& typeName = ctx.generatedTypeNames[typeId];
    ss << std::format(R"(
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
    auto const& data = ptr.as<{0}>();
    return data.size();
}
static void encodeArrayEnterElem(
    ao::schema::cpp::CppEncodeRuntime& runtime,
    ao::schema::cpp::AnyPtr ptr,
    uint32_t i) {
    auto const& data = ptr.as<{0}>():
    if (i >= data.size()) {
        ao::schema::cpp::cppRuntimeFail(runtime, ao::pack::Error::BadData);
        return;
    }
    runtime.encodeStack.emplace_back({
        .kind = ao::schema::cpp::FrameKind::ArrayElement,
        .ops = &({1}::encode),
        .data = AnyPtr{(void const*)&data[i]},
    });
}
static void encodeArrayExitElem(
    ao::schema::cpp::CppEncodeRuntime& runtime,
    ao::schema::cpp::AnyPtr ptr) {
    if (runtime.encodeStack.empty()) {
        ao::schema::cpp::cppRuntimeFail(runtime, ao::pack::Error::BadData)
        return;
    }
    runtime.encodeStack.pop_back();
}

static auto encode = ao::schema::cpp::EncodeTypeOps{
    .arrayEnter = &encodeArrayEnter,
    .arrayExit = &encodeArrayExit,
    .arrayLen = &encodeArrayLen,
    .arrayEnterElem = &encodeArrayEnterElem,
    .arrayExitElem = &encodeArrayExitElem, 
};

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
    auto& data = ptr.as<{0}>();
    data.resize(len);
}
static void decodeArrayEnterElem(CppDecodeRuntime& runtime,
					      MutPtr ptr,
					      uint32_t i) {
    auto& data = ptr.as<{0}>();
    if (i >= data.size()) {
        ao::schema::cpp::cppRuntimeFail(runtime, ao::pack::Error::BadData);
        return;
    }
    runtime.encodeStack.emplace_back({
        .kind = ao::schema::cpp::FrameKind::ArrayElement,
        .ops = &({1}::decode),
        .data = MutPtr{(void*)&data[i]},
    });
}
static void decodeArrayExitElem(CppDecodeRuntime& runtime, MutPtr ptr) {
    if (runtime.encodeStack.empty()) {
        ao::schema::cpp::cppRuntimeFail(runtime, ao::pack::Error::BadData)
        return;
    }
    runtime.encodeStack.pop_back();
}

static auto decode = ao::schema::cpp::DecodeTypeOps{
    .arrayEnter = &decodeArrayEnter,
    .arrayExit = &decodeArrayExit,
    .arrayPrepare = &decodeArrayPrepare,
    .arayEnterElem = &decodeArrayEnterElem,
    .arrayExitElem = &decodeArrayExitElem
};
)",
                      typeName, subtypeAccessor);
}

static const std::string_view encodeRuntime =
    "ao::schema::cpp:CppEncodeRuntime";
static const std::string_view anyPtr = "ao::schema::cpp:CppEncodeRuntime";
static const std::string_view decodeRuntime =
    "ao::schema::cpp:CppDecodeRuntime";
static const std::string_view mutPtr = "ao::schema::cpp:CppEncodeRuntime";

template <class... T>
std::string funcSig(std::string_view ret, std::string_view name, T... args) {
    size_t idx = 0;
    std::string ret = std::format("static {} {}(", ret, name);
    ((idx += 1, std::format(idx == 1 ? "{}" : ", {}")), ...);
    ret += ")";
    return ret;
}

static void encodeFieldBegin(CppCodeGenContext& ctx,
                             std::stringstream& ss,
                             size_t typeId,
                             IdFor<ir::Message> v) {
    ss << funcSig("void", "encodeFieldBegin",
                  std::string{encodeRuntime} + "runtime",
                  std::string{anyPtr} + "ptr", "uint32_t fieldId")
       << " {\n";

    ss << "\tswitch (fieldId) {\n";
    enumerate(ctx.ir.messages[v.idx].fields,
              [&](size_t fieldId, IdFor<ir::Field> const& globalFieldId) {
                  auto const& fieldDesc = ctx.ir.fields[globalFieldId.idx];
                  ss << "case " << fieldId << ": {\n";
                  ss << std::format("auto const& data = ptr.as<{}>();\n",
                                    ctx.generatedTypeNames[typeId]);
                  ss << std::format("auto fieldPtr = &data.{}",
                                    ctx.ir.strings[fieldDesc.name.idx]);
                  ss << std::format(R"(
runtime.encodeStack.emplace_back({
    .kind = ao::schema::cpp::FrameKind::Field,
    .ops = &CppAccessor<{0}>::encode,
    .data = {fieldPtr},
});
)",
                                    fieldDesc.type.idx);
                  ss << "} break;\n";
              });
    ss << R"(
default: 
	ao::schema::cpp::cppRuntimeFail(runtime, ao::pack::Error::BadData)
	return;
)";
    ss << "\t}\n";
    ss << "}\n";
}
static void decodeFieldBegin(CppCodeGenContext& ctx,
                             std::stringstream& ss,
                             IdFor<ir::Message> v) {
    ss << funcSig("void", "decodeFieldBegin",
                  std::string{decodeRuntime} + "runtime",
                  std::string{mutPtr} + "ptr", "uint32_t fieldId")
       << " {\n";

    ss << "}\n";
}

static void generateTypeAccessorMessage(CppCodeGenContext& ctx,
                                        std::stringstream& ss,
                                        size_t typeId,
                                        IdFor<ir::Message> v) {
    auto const& msgDesc = ctx.ir.messages[v.idx];
    auto const& typeName = ctx.generatedTypeNames[typeId];
    ss << funcSig("void", "encodeMsgBegin",
                  std::string{encodeRuntime} + "runtime",
                  std::string{anyPtr} + "ptr", "uint32_t msgId")
       << " {}\n";
    ss << funcSig("void", "encodeMsgEnd",
                  std::string{encodeRuntime} + "runtime",
                  std::string{anyPtr} + "ptr", "uint32_t msgId")
       << " {}\n";
    ss << funcSig("void", "decodeMsgBegin",
                  std::string{decodeRuntime} + "runtime",
                  std::string{mutPtr} + "ptr", "uint32_t msgId")
       << " {}\n";
    ss << funcSig("void", "decodeMsgEnd",
                  std::string{decodeRuntime} + "runtime",
                  std::string{mutPtr} + "ptr", "uint32_t msgId")
       << " {}\n";

    encodeFieldBegin(ctx, ss, typeId, v);
    ss << funcSig("void", "encodeFieldEnd",
                  std::string{encodeRuntime} + "runtime",
                  std::string{anyPtr} + "ptr")
       << "{ runtime.encodeStack.pop_back(); }\n";

    decodeFieldBegin(ctx, ss, v);
    ss << funcSig("void", "decodeFieldEnd",
                  std::string{decodeRuntime} + "runtime",
                  std::string{mutPtr} + "ptr")
       << "{ runtime.encodeStack.pop_back(); }\n";
}

static std::string generateTypeAccessors(CppCodeGenContext& ctx,
                                         size_t typeId,
                                         ir::Type const& type) {
    std::stringstream ss;
    auto const& typeName = ctx.generatedTypeNames[typeId];

    ss << "template<> class CppAccessor<" << typeId << "> {\n";
    std::visit(Overloaded{
                   [&](ir::Scalar const& v) {
                       generateTypeAccessorScalar(ctx, ss, typeId, v);
                   },
                   [&](ir::Array const& v) {
                       generateTypeAccessorArray(ctx, ss, typeId, v);
                   },
                   [&](ir::Optional const& v) {
                       generateTypeAccessorsOptional(ctx, ss, typeId, v);
                   },
                   [&](IdFor<ir::OneOf> const& v) {
                       generateTypeAccessorsOneof(ctx, ss, typeId,
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

void generateCppCode(CppCodeGenContext& ctx, std::ostream& out) {
    out << R"(#include <vector>
#include <variant>
#include <cstdint>
#include <ao/schema/CppAdapter.h>

namespace aosl_detail {
)";

    for (auto const& def : ctx.generatedTypeDecls) {
        out << def << "\n";
    }

    for (auto const& def : ctx.generatedTypeDefs) {
        out << def << "\n";
    }

    out << "\n}\n";

    for (auto const& def : ctx.generatedMessages) {
        out << def << "\n";
    }
}

bool generateCppCode(ir::IR const& ir, ErrorContext& errs, std::ostream& out) {
    if (!errs.ok())
        return false;

    CppCodeGenContext ctx{ir, errs};
    enumerate(ir.types, [&ctx](size_t i, auto const& type) {
        auto typeName = generateTypeName(ctx, i, type);
        ctx.generatedTypeNames.emplace_back(std::move(typeName));
    });
    enumerate(ir.types, [&ctx](size_t i, auto const& type) {
        auto typeDef = generateTypeDef(ctx, i, type);
        if (!typeDef)
            return;
        ctx.generatedTypeDefs.emplace_back(std::move(*typeDef));
    });
    enumerate(ir.types, [&ctx](size_t i, auto const& type) {
        auto typeDecl = generateTypeDecl(ctx, i, type);
        if (!typeDecl)
            return;
        ctx.generatedTypeDecls.emplace_back(std::move(*typeDecl));
    });
    enumerate(ir.types, [&ctx](size_t i, auto const& type) {
        auto msg = std::get_if<IdFor<ir::Message>>(&type.payload);
        if (!msg)
            return;
        auto msgForward = generateNamespaceForwarding(ctx, i, *msg);
        if (!msgForward)
            return;
        ctx.generatedMessages.emplace_back(std::move(*msgForward));
    });

    generateCppCode(ctx, out);
    return errs.ok();
}
}  // namespace ao::schema::cpp
