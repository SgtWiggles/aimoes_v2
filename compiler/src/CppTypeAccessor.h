#include "CppBackendHelpers.h"

#include <string>
#include <string_view>
#include <variant>

#include "ao/schema/IR.h"
#include "ao/utils/Overloaded.h"

inline constexpr std::string_view TYPE_ACCESSOR_TEMPLATE = R"(
struct @NAME {
	static ao::schema::cpp::EncodeTypeOps const encode;
	static ao::schema::cpp::DecodeTypeOps const decode;
};
)";

void generateTypeAccessorOptional(CppCodeGenContext& ctx,
                                  size_t typeId,
                                  ao::schema::ir::Optional const& v);
void generateTypeAccessorScalar(CppCodeGenContext& ctx,
                                size_t typeId,
                                ao::schema::ir::Scalar const& scalar);
void generateTypeAccessorArray(CppCodeGenContext& ctx,
                               size_t typeId,
                               ao::schema::ir::Array const& v);

void generateTypeAccessorOneof(CppCodeGenContext& ctx,
                               size_t typeId,
                               ao::schema::ir::OneOf const& oneofDesc);

void generateTypeAccessorMessage(CppCodeGenContext& ctx,
                                 size_t typeId,
                                 ao::schema::IdFor<ao::schema::ir::Message> v);

GeneratedObject generateAccessorDecl(CppCodeGenContext& ctx,
                                     size_t typeId,
                                     ao::schema::ir::Type const& type);
void generateTypeAccessor(CppCodeGenContext& ctx,
                           size_t typeId,
                           ao::schema::ir::Type const& type);
