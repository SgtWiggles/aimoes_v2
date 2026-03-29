#include "CppTypeAccessor.h"

using namespace ao;
using namespace ao::schema;

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
    std::string ret = std::format("{} {}(", sig, name);
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
    auto signature = funcSig("void", std::format("encodeFieldBegin_{}", typeId),
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
    ss << funcSig("void", std::format("decodeFieldBegin_{}", typeId),
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

void generateTypeAccessorMessage(CppCodeGenContext& ctx,
                                 size_t typeId,
                                 IdFor<ir::Message> v) {
    std::stringstream ss;
    auto const& msgDesc = ctx.ir.messages[v.idx];
    auto const& typeName = ctx.generatedTypeNames[typeId];
    auto& accessor = ctx.generatedAccessors[typeId];
    ss << funcSig("void", std::format("encodeMsgBegin_{}", typeId),
                  std::string{encodeRuntime} + "& runtime",
                  std::string{anyPtr} + " ptr", "uint32_t msgId")
       << " {}\n";
    ss << funcSig("void", std::format("encodeMsgEnd_{}", typeId),
                  std::string{encodeRuntime} + "& runtime",
                  std::string{anyPtr} + " ptr")
       << " {}\n";
    ss << funcSig("void", std::format("decodeMsgBegin_{}", typeId),
                  std::string{decodeRuntime} + "& runtime",
                  std::string{mutPtr} + " ptr", "uint32_t msgId")
       << " {}\n";
    ss << funcSig("void", std::format("decodeMsgEnd_{}", typeId),
                  std::string{decodeRuntime} + "& runtime",
                  std::string{mutPtr} + " ptr")
       << " {}\n";

    encodeFieldBegin(ctx, ss, typeId, v);
    ss << funcSig("void", std::format("encodeFieldEnd_{}", typeId),
                  std::string{encodeRuntime} + "& runtime",
                  std::string{anyPtr} + " ptr")
       << "{ runtime.stack.pop_back(); }\n";

    decodeFieldBegin(ctx, ss, typeId, v);
    ss << funcSig("void", std::format("decodeFieldEnd_{}", typeId),
                  std::string{decodeRuntime} + "& runtime",
                  std::string{mutPtr} + " ptr")
       << "{ runtime.stack.pop_back(); }\n";

    ss << replaceMany(R"(
ao::schema::cpp::EncodeTypeOps const @QNAME::encode = ao::schema::cpp::EncodeTypeOps{
 .msgBegin = &encodeMsgBegin_@TYPE_ID,
 .msgEnd = &encodeMsgEnd_@TYPE_ID,
 .fieldBegin = &encodeFieldBegin_@TYPE_ID,
 .fieldEnd = &encodeFieldEnd_@TYPE_ID,
};
ao::schema::cpp::DecodeTypeOps const @QNAME::decode = ao::schema::cpp::DecodeTypeOps{
 .msgBegin = &decodeMsgBegin_@TYPE_ID,
 .msgEnd = &decodeMsgEnd_@TYPE_ID,
 .fieldBegin = &decodeFieldBegin_@TYPE_ID,
 .fieldEnd = &decodeFieldEnd_@TYPE_ID,
};
)",
                      {
                          {"@TYPE_ID", std::to_string(typeId)},
                          {"@QNAME", accessor.name.qualifiedName()},
                      });
    accessor.impl = ss.str();
}
