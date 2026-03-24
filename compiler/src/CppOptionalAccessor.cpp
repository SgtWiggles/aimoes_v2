#include "CppBackendHelpers.h"

#include <string>
#include <variant>

#include "ao/schema/IR.h"
#include "ao/utils/Overloaded.h"

using namespace ao;
using namespace ao::schema;

static std::string const optionalImplTemplate = R"(
bool encodeOptionalHasValue_@TYPE_ID(
 ao::schema::cpp::CppEncodeRuntime& runtime,
 ao::schema::cpp::AnyPtr ptr) {
 return ptr.as<@TYPE_NAME>().has_value();
}
void encodeOptionalEnter_@TYPE_ID(
 ao::schema::cpp::CppEncodeRuntime& runtime,
 ao::schema::cpp::AnyPtr ptr) {
}
void encodeOptionalExit_@TYPE_ID(
 ao::schema::cpp::CppEncodeRuntime& runtime,
 ao::schema::cpp::AnyPtr ptr) {
}
void encodeOptionalEnterValue_@TYPE_ID(
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
void encodeOptionalExitValue_@TYPE_ID(ao::schema::cpp::CppEncodeRuntime& runtime, ao::schema::cpp::AnyPtr ptr) {
 runtime.stack.pop_back();
}

void decodeOptionalEnter_@TYPE_ID(
 ao::schema::cpp::CppDecodeRuntime& runtime,
 ao::schema::cpp::MutPtr ptr) {
 auto& data = ptr.as<@TYPE_NAME>();
 data.reset();
}
void decodeOptionalExit_@TYPE_ID(
 ao::schema::cpp::CppDecodeRuntime& runtime,
 ao::schema::cpp::MutPtr ptr) {
 // Do nothing
}
void decodeOptionalSetPresent_@TYPE_ID(
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

void decodeOptionalEnterValue_@TYPE_ID(
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
void decodeOptionalExitValue_@TYPE_ID(
 ao::schema::cpp::CppDecodeRuntime& runtime,
 ao::schema::cpp::MutPtr ptr) {
 runtime.stack.pop_back();
}

ao::schema::cpp::EncodeTypeOps const @ACCESSOR_QNAME::encode = ao::schema::cpp::EncodeTypeOps{
 .optionalHasValue = &encodeOptionalHasValue_@TYPE_ID,
 .optionalEnter = &encodeOptionalEnter_@TYPE_ID,
 .optionalExit = &encodeOptionalExit_@TYPE_ID,
 .optionalEnterValue = &encodeOptionalEnterValue_@TYPE_ID,
 .optionalExitValue = &encodeOptionalExitValue_@TYPE_ID,
};


ao::schema::cpp::DecodeTypeOps const @ACCESSOR_QNAME::decode = ao::schema::cpp::DecodeTypeOps{
 .optionalEnter = &decodeOptionalEnter_@TYPE_ID,
 .optionalExit = &decodeOptionalExit_@TYPE_ID,
 .optionalSetPresent = &decodeOptionalSetPresent_@TYPE_ID,
 .optionalEnterValue = &decodeOptionalEnterValue_@TYPE_ID,
 .optionalExitValue = &decodeOptionalExitValue_@TYPE_ID,
};
)";

void generateTypeAccessorOptional(CppCodeGenContext& ctx,
                                  size_t typeId,
                                  ir::Optional const& v) {
    auto& accessorObj = ctx.generatedAccessors[typeId];
    auto subtypeId = ctx.generatedAccessors[v.type.idx].name.qualifiedName();
    auto const& typeName = ctx.generatedTypeNames[typeId];

    accessorObj.impl =
        replaceMany(optionalImplTemplate,
                    {
                        {"@TYPE_NAME", typeName.qualifiedName()},
                        {"@SUBTYPE_ACCESSOR", subtypeId},
                        {"@ACCESSOR_QNAME", accessorObj.name.qualifiedName()},
                        {"@TYPE_ID", std::to_string(typeId)},
                    });
}
