add_library(compiler OBJECT
 "src/Ast.cpp"
 "src/SemanticContext.cpp"
 "src/AstValidateIds.h"
 "src/AstValidateIds.cpp"
 "src/ComputeDirectives.h"
 "src/ComputeDirectives.cpp"
 "src/IR.cpp"
 "include/ao/schema/IR.h"
 "include/ao/schema/ResourceCache.h"
 "src/ComputeNormalizedTypeParameters.cpp" "include/ao/schema/Parser.h" "src/Parser.cpp")
target_include_directories(compiler PUBLIC include)
target_link_libraries(compiler PUBLIC Boost::container_hash lexy)

add_executable(CompilerTests
 "tests/AstTests.cpp"
 "tests/ImportTests.cpp"
 "tests/ResolveSymbolsTests.cpp"
 "tests/ResolveSymbolsOneOfTests.cpp"
 "tests/Helpers.h"
 "tests/Helpers.cpp"
 "tests/ValidateIdTests.cpp"
 "tests/ComputeDirectiveTests.cpp"
 "tests/AstQueries.h"
 "tests/AstQueries.cpp")
target_link_libraries(CompilerTests PRIVATE compiler Catch2::Catch2WithMain)
