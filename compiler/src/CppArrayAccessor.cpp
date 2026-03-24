#include "CppTypeAccessor.h"

void generateTypeAccessorArray(CppCodeGenContext& ctx,
                               size_t typeId,
                               ao::schema::ir::Array const& v) {
    auto subtypeAccessor =
        ctx.generatedAccessors[v.type.idx].name.qualifiedName();
    auto& accessor = ctx.generatedAccessors[typeId];
    auto const& typeName = ctx.generatedTypeNames[typeId];
    accessor.impl = replaceMany(R"(
static void encodeArrayEnter_@TYPE_ID(
 ao::schema::cpp::CppEncodeRuntime& runtime,
 ao::schema::cpp::AnyPtr ptr,
 uint32_t typeId) {
 // Do nothing
}
static void encodeArrayExit_@TYPE_ID(
 ao::schema::cpp::CppEncodeRuntime& runtime,
 ao::schema::cpp::AnyPtr ptr) {
 // Do nothing
}
static uint32_t encodeArrayLen_@TYPE_ID(
 ao::schema::cpp::CppEncodeRuntime& runtime,
 ao::schema::cpp::AnyPtr ptr) {
 auto const& data = ptr.as<@TYPE_NAME>();
 return data.size();
}
static void encodeArrayEnterElem_@TYPE_ID(
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
static void encodeArrayExitElem_@TYPE_ID(
 ao::schema::cpp::CppEncodeRuntime& runtime,
 ao::schema::cpp::AnyPtr ptr) {
 if (runtime.stack.empty()) {
 ao::schema::cpp::cppRuntimeFail(runtime, ao::pack::Error::BadData)
 return;
 }
 runtime.stack.pop_back();
}


static void decodeArrayEnter_@TYPE_ID(CppDecodeRuntime& runtime,
				 MutPtr ptr,
				 uint32_t typeId) {
 // Do nothing
}
static void decodeArrayExit_@TYPE_ID(CppDecodeRuntime& runtime, MutPtr ptr) {
 // Do nothing
}
static void decodeArrayPrepare_@TYPE_ID(CppDecodeRuntime& runtime,
				 MutPtr ptr,
				 uint32_t len) {
 auto& data = ptr.as<@TYPE_NAME>();
 data.resize(len);
}
static void decodeArrayEnterElem_@TYPE_ID(CppDecodeRuntime& runtime,
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
static void decodeArrayExitElem_@TYPE_ID(CppDecodeRuntime& runtime, MutPtr ptr) {
 if (runtime.stack.empty()) {
 ao::schema::cpp::cppRuntimeFail(runtime, ao::pack::Error::BadData)
 return;
 }
 runtime.stack.pop_back();
}

ao::schema::cpp::EncodeTypeOps const @QNAME::encode = ao::schema::cpp::EncodeTypeOps{
 .arrayEnter = &encodeArrayEnter_@TYPE_ID,
 .arrayExit = &encodeArrayExit_@TYPE_ID,
 .arrayLen = &encodeArrayLen_@TYPE_ID,
 .arrayEnterElem = &encodeArrayEnterElem_@TYPE_ID,
 .arrayExitElem = &encodeArrayExitElem_@TYPE_ID, 
};

ao::schema::cpp::DecodeTypeOps const @QNAME::decode = ao::schema::cpp::DecodeTypeOps{
 .arrayEnter = &decodeArrayEnter_@TYPE_ID,
 .arrayExit = &decodeArrayExit_@TYPE_ID,
 .arrayPrepare = &decodeArrayPrepare_@TYPE_ID,
 .arayEnterElem = &decodeArrayEnterElem_@TYPE_ID,
 .arrayExitElem = &decodeArrayExitElem_@TYPE_ID
};
)",
                                {
                                    {"@TYPE_NAME", typeName.qualifiedName()},
                                    {"@SUBTYPE_ACCESSOR", subtypeAccessor},
                                    {"@TYPE_ID", std::to_string(typeId)},
                                    {"@QNAME", accessor.name.qualifiedName()},
                                });
}
