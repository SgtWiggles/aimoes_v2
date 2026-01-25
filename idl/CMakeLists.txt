add_library(idl OBJECT
	"src/Parser.cpp"
	"src/CompilationState.cpp"
)
target_include_directories(idl PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(idl PUBLIC foonathan::lexy)

add_executable(idltests 
	tests/main.cpp
)
target_link_libraries(idltests PRIVATE idl Catch2::Catch2WithMain)

