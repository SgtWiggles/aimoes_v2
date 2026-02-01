add_library(compiler OBJECT
 "src/Ast.cpp"
 "src/CompilerContext.cpp"
 "src/AstValidateIds.h"
 "src/AstValidateIds.cpp" "src/ComputeDirectives.h" "src/ComputeDirectives.cpp")
target_include_directories(compiler PUBLIC include)

add_executable(CompilerTests
 "tests/AstTests.cpp"
 "tests/ImportTests.cpp"
 "tests/ResolveSymbolsTests.cpp"
 "tests/ResolveSymbolsOneOfTests.cpp"
 "tests/Helpers.h"
 "tests/Helpers.cpp" "tests/ValidateIdTests.cpp" "tests/ComputeDirectiveTests.cpp" "tests/AstQueries.h" "tests/AstQueries.cpp")
target_link_libraries(CompilerTests PRIVATE compiler Catch2::Catch2WithMain)
