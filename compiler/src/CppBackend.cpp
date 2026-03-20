#include "ao/schema/CppBackend.h"

#include "ao/schema/CppAdapter.h"

#include <fstream>
#include <sstream>

#include "ao/utils/Array.h"
#include "ao/utils/Overloaded.h"

#include "ao/pack/OStreamWriteStream.h"

namespace ao::schema::cpp {

void replaceAll(std::string& str,
                std::string_view key,
                std::string_view value) {
    if (key.empty())
        return;  // avoid infinite loop

    size_t pos = 0;
    while ((pos = str.find(key, pos)) != std::string::npos) {
        str.replace(pos, key.length(), value);
        pos += value.length();  // advance past the replacement
    }
}

std::string replaceMany(
    std::string_view str,
    std::initializer_list<std::pair<std::string_view, std::string_view>>
        replacements) {
    auto ret = std::string{str};
    for (const auto& [key, value] : replacements) {
        replaceAll(ret, key, value);
    }
    return ret;
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
std::optional<std::string_view> getMessageName(std::string_view name) {
    auto parts = parsePackageName(name);
    if (parts.empty())
        return {};
    return parts.back();
}
std::optional<std::string> getNamespaceName(std::string_view name) {
    auto packageName = parsePackageName(name);
    if (packageName.empty())
        return {};

    auto namespaceName = std::string{};
    for (size_t idx = 0; idx < packageName.size() - 1; ++idx) {
        if (!namespaceName.empty())
            namespaceName += "::";
        namespaceName += packageName[idx];
    }

    return namespaceName;
}

struct TypeName {
    std::string ns;
    std::string name;
    std::string qualifiedName() const {
        if (ns.empty())
            return name;
        return std::format("{}::{}", ns, name);
    }
};

struct CppCodeGenContext {
    ir::IR const& ir;
    ErrorContext& errs;

    std::vector<TypeName> generatedTypeNames;
    std::vector<std::string> generatedTypeDecls;
    std::vector<std::string> generatedTypeDefs;
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

static TypeName generateTypeName(CppCodeGenContext& ctx,
                                 size_t typeId,
                                 ir::Type const& type) {
    return std::visit(
        Overloaded{
            [&ctx](ir::Scalar const& v) -> TypeName {
                auto cppWidth = getCppBitWidth(ctx, v.width);
                switch (v.kind) {
                    case ir::Scalar::BOOL:
                        return {{}, "bool"};
                    case ir::Scalar::INT:
                        return {{}, std::format("int{}_t", cppWidth)};
                    case ir::Scalar::UINT:
                        return {{}, std::format("uint{}_t", cppWidth)};
                    case ir::Scalar::F32:
                        return {{}, "float"};
                    case ir::Scalar::F64:
                        return {{}, "double"};
                    case ir::Scalar::CHAR:
                        return {{}, "char"};
                    case ir::Scalar::BYTE:
                        return {{}, "std::byte"};
                    default:
                        ctx.errs.fail({
                            .code = ErrorCode::INTERNAL,
                            .message = std::format(
                                "Unknown C++ scalar type: {}", (int)v.kind),
                            .loc = {},
                        });
                        break;
                }

                return {{}, "<error>"};
            },
            [typeId, &ctx](ir::Array const& v) -> TypeName {
                auto scalar =
                    std::get_if<ir::Scalar>(&ctx.ir.types[v.type.idx].payload);
                if (scalar) {
                    if (scalar->kind == ir::Scalar::CHAR)
                        return {{}, "std::string"};
                    if (scalar->kind == ir::Scalar::BYTE)
                        return {{}, "std::vector<std::byte>"};
                }
                return {"aosl_detail", std::format("Type_{}_Arr", typeId)};
            },
            [typeId](ir::Optional const& v) -> TypeName {
                return {"aosl_detail", std::format("Type_{}_Opt", typeId)};
            },
            [typeId](IdFor<ir::OneOf> const& v) -> TypeName {
                return {"aosl_detail", std::format("Type_{}_Oneof", typeId)};
            },
            [&ctx, typeId](IdFor<ir::Message> const& v) -> TypeName {
                auto const& fqn =
                    ctx.ir.strings[ctx.ir.messages[v.idx].name.idx];
                auto messageName = getMessageName(fqn);
                auto namespaceName = getNamespaceName(fqn);
                if (!messageName)
                    ctx.errs.fail({
                        .code = ao::schema::ErrorCode::INTERNAL,
                        .message = std::format(
                            "Got message '{}' with namespace '{}' with empty "
                            "name!",
                            fqn, namespaceName.value_or(std::string{""})),
                        .loc = {},
                    });
                return {namespaceName.value_or(std::string{}),
                        std::string{messageName.value_or(std::string_view{})}};
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
                auto const& fqn =
                    ctx.ir.strings[ctx.ir.messages[v.idx].name.idx];
                auto messageName = getMessageName(fqn);
                auto namespaceName = getNamespaceName(fqn);

                return replaceMany(
                    R"(namespace @NAMESPACE {
    struct @MSG;
}
)",
                    {
                        {"@NAMESPACE", *namespaceName},
                        {"@MSG", *messageName},
                    });
            },
        },
        type.payload);
}

static void generateMessageDirectives(CppCodeGenContext& ctx,
                                      std::stringstream& ss,
                                      size_t typeId,
                                      IdFor<ir::Message> const& msgIdx) {
    auto const& msg = ctx.ir.messages[msgIdx.idx];
    auto const& directiveSet = ctx.ir.directiveSets[msg.directives.idx];
    ir::DirectiveProfile const* directiveProfile = nullptr;
    for (auto const& profileIdx : directiveSet.directives) {
        auto const& profile = ctx.ir.directiveProfiles[profileIdx.idx];
        if (profile.domain != ir::DirectiveProfile::Cpp)
            continue;
        directiveProfile = &profile;
        break;
    }
    if (!directiveProfile)
        return;

    for (auto const& propertyIdx : directiveProfile->properties) {
        auto const& property = ctx.ir.directiveProperties[propertyIdx.idx];
        auto const& name = ctx.ir.strings[property.name.idx];
        if (name == "compare") {
            auto value = std::get_if<IdFor<std::string>>(&property.value.value);
            if (!value)
                continue;
            auto const& compareMode = ctx.ir.strings[value->idx];
            auto const& selfTypeName = ctx.generatedTypeNames[typeId].name;
            if (compareMode == "none") {
                continue;
            } else if (compareMode == "default") {
                ss << std::format(
                    "friend auto "
                    "operator<=>({0} "
                    "const&, {0} const&) "
                    " = "
                    "default",
                    selfTypeName);
            } else if (compareMode == "define") {
                ss << std::format(
                    "friend auto "
                    "operator<=>({0} "
                    "const&, {0} const&) ",
                    selfTypeName);
            } else {
                continue;
            }
            ss << ";\n";
        } else if (name == "define") {
            auto value = std::get_if<IdFor<std::string>>(&property.value.value);
            if (!value)
                continue;
            auto const& definition = ctx.ir.strings[value->idx];
            ss << definition << ";\n";
        }
    }
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
                return std::format(
                    "namespace aosl_detail {{ using {} = std::vector<{}>; }}",
                    ctx.generatedTypeNames[typeId].name,
                    ctx.generatedTypeNames[v.type.idx].qualifiedName());
            },
            [&ctx,
             typeId](ir::Optional const& v) -> std::optional<std::string> {
                return std::format(
                    "namespace aosl_detail {{ using {} = std::optional<{}>; }}",
                    ctx.generatedTypeNames[typeId].name,
                    ctx.generatedTypeNames[v.type.idx].qualifiedName());
            },
            [&ctx,
             typeId](IdFor<ir::OneOf> const& v) -> std::optional<std::string> {
                std::stringstream ss;
                ss << std::format(
                    "namespace aosl_detail {{ using {} = "
                    "std::variant<std::monostate",
                    ctx.generatedTypeNames[typeId].name);
                for (auto const& armField : ctx.ir.oneOfs[v.idx].arms) {
                    ss << ", ";
                    auto const& arm = ctx.ir.fields[armField.idx];
                    ss << "\n "
                       << ctx.generatedTypeNames[arm.type.idx].qualifiedName();
                }
                ss << "\n>; }\n";
                return ss.str();
            },
            [&ctx, typeId](
                IdFor<ir::Message> const& v) -> std::optional<std::string> {
                auto const& msg = ctx.ir.messages[v.idx];
                auto const& typeName = ctx.generatedTypeNames[typeId];
                std::stringstream ss;
                if (!typeName.ns.empty())
                    ss << "namespace " << typeName.ns << "{";

                ss << std::format("struct {} {{\n", typeName.name);
                for (auto const& fieldId : msg.fields) {
                    auto const& field = ctx.ir.fields[fieldId.idx];
                    auto const& fieldName = ctx.ir.strings[field.name.idx];
                    auto const& fieldTypeName =
                        ctx.generatedTypeNames[field.type.idx];
                    ss << std::format(" {} {};\n",
                                      fieldTypeName.qualifiedName(), fieldName);
                }
                ss << std::format(
                    "using Accessor = aosl_detail::CppAccessor<{}>;\n", typeId);
                ss << std::format(
                    "static constexpr uint32_t AOSL_MESSAGE_ID = {};\n", v.idx);
                ss << std::format(
                    "static constexpr uint32_t AOSL_TYPE_ID = {};\n", typeId);

                generateMessageDirectives(ctx, ss, typeId, v);

                ss << "};";

                if (!typeName.ns.empty())
                    ss << "}";
                return ss.str();
            },
        },
        type.payload);
}

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
 .ops = &(CppAccessor<@SUBTYPE_ID>::encode),
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
 .ops = &(CppAccessor<@SUBTYPE_ID>::decode),
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
    auto subtypeId = std::format("{}", v.type.idx);
    auto const& typeName = ctx.generatedTypeNames[typeId];

    ss << replaceMany(optionalStringTemplate,
                      {
                          {"@TYPE_NAME", typeName.qualifiedName()},
                          {"@SUBTYPE_ID", subtypeId},
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
			auto encodePtr = &CppAccessor<@SUBTYPE_ID>::encode;
			auto dataPtr = (void const*)(&std::get<@FIELD_ID +1>(data));
			runtime.stack.emplace_back(ao::schema::encode::EncodeFrame{
				.ops = encodePtr,
				.data = ao::schema::cpp::AnyPtr{dataPtr},
			});
		} break;
)",
                        {
                            {"@FIELD_ID", std::to_string(idx)},
                            {"@SUBTYPE_ID", std::to_string(fieldDesc.type.idx)},
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
        encodeOneofEnterArm +=
            replaceMany(R"(
		case @FIELD_ID: {
			auto ops = &CppAccessor<@SUBTYPE_ID>::decode;
 data.emplace<@FIELD_ID +1>();
		} break;
)",
                        {
                            {"@FIELD_ID", std::to_string(idx)},
                            {"@SUBTYPE_ID", std::to_string(fieldDesc.type.idx)},
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
			auto ops = &CppAccessor<@SUBTYPE_ID>::decode;
 auto value = std::get_if<@FIELD_ID +1>(&data);
			runtime.stack.emplace_back(ao::schema::cpp::DecodeFrame{
				.ops = ops,
				.data = ao::schema::cpp::MutPtr{(void*)value},
			});
		} break;
)",
            {
                {"@FIELD_ID", std::to_string(idx)},
                {"@SUBTYPE_ID", std::to_string(fieldDesc.type.idx)},
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
    auto subtypeAccessor = std::format("CppAccessor<{}>", v.type.idx);
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
		.ops = &CppAccessor<@SUBTYPE_ID>::encode,
		.data = {fieldPtr},
	});
} break;

)",
                      {
                          {"@FIELD_ID", std::to_string(globalFieldId.idx)},
                          {"@FIELD_NAME", ctx.ir.strings[fieldDesc.name.idx]},
                          {"@SUBTYPE_ID", std::to_string(fieldDesc.type.idx)},
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
		.ops = &(CppAccessor<@SUBTYPE_ID>::decode),
		.data = {fieldPtr},
	});
 
} break;
)",
            {
                {"@FIELD_ID", std::to_string(fieldId.idx)},
                {"@SUBTYPE_ID", std::to_string(fieldDesc.type.idx)},
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

void generateCppCode(CppCodeGenContext& ctx, std::ostream& out) {
    out << R"(
#pragma once
#include <vector>
#include <variant>
#include <cstdint>

#include <ao/schema/CppAdapter.h>
#include <ao/schema/IR.h>

namespace aosl_detail {
template<size_t> class CppAccessor;
}

)";

    out << replaceMany(R"(
constexpr inline ao::schema::ir::IRHeader getCompiledHeader() {
 return ao::schema::ir::IRHeader{
 .magic = @MAGIC,
 .version = @VERSION,
 };
};
static_assert(getCompiledHeader() == ao::schema::ir::IRHeader{}, "Generated code was not the same as current code, regenerate the schema");
)",
                       {
                           {"@MAGIC", std::to_string(ir::IRHeader{}.magic)},
                           {"@VERSION", std::to_string(ir::IRHeader{}.version)},
                       });

    for (auto const& def : ctx.generatedTypeDecls) {
        out << def << "\n";
    }

    for (auto const& def : ctx.generatedTypeDefs) {
        out << def << "\n";
    }

    out << "namespace aosl_detail {\n";
    out << "template<size_t> struct CppAccessor;\n";
    enumerate(ctx.ir.types, [&](size_t typeId, auto const& type) {
        out << generateTypeAccessors(ctx, typeId, type) << "\n";
    });
    out << "\n}\n";
}

bool generateCppCode(ir::IR const& ir, ErrorContext& errs, OutputFiles& files) {
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

    generateCppCode(ctx, files.header);

    ao::pack::byte::OStreamWriteStream irWs(files.ir);
    ir::serializeIRFile(irWs, ir);
    if (files.irHeader) {
        std::vector<std::byte> bytes;
        bytes.resize(irWs.byteSize());
        ao::pack::byte::WriteStream ws{std::span{bytes.data(), bytes.size()}};
        ir::serializeIRFile(ws, ir);
        (*files.irHeader) << "{";
        size_t i = 0;
        for (auto const& b : bytes) {
            if (i % 256 == 0)
                (*files.irHeader) << "\n";
            (*files.irHeader)
                << std::format("'\\x{:02x}', ", static_cast<uint8_t>(b));
        }
        (*files.irHeader) << "}";
    }

    return errs.ok();
}
}  // namespace ao::schema::cpp
