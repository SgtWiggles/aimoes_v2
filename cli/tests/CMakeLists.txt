
 add_executable(AoslCppSimple "simple/main.cpp" "CppTestHelpers.h")
 target_link_libraries(AoslCppSimple PUBLIC Catch2::Catch2WithMain compiler)
 target_include_directories(AoslCppSimple PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
 message(STATUS "AOSL input = ${CMAKE_CURRENT_SOURCE_DIR}/simple/messages.aosl")
 aosl_generate(AoslCppSimple "${CMAKE_CURRENT_BINARY_DIR}/simple/"
     FILE_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/simple"
     INPUTS
         "${CMAKE_CURRENT_SOURCE_DIR}/simple/messages.aosl"
 )
