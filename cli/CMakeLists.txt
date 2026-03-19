add_executable(aosl-compiler
	"src/main.cpp"
)

target_link_libraries(aosl-compiler PUBLIC compiler Boost::program_options)

# Configure a header so tests can find the build/source directories and compiler
configure_file(
 "${CMAKE_CURRENT_SOURCE_DIR}/tests/PathConfig.h.in"
 "${CMAKE_CURRENT_BINARY_DIR}/PathConfig.h"
 @ONLY
)

# Add integration test executable
# add_executable(CliIntegrationTests)
# target_include_directories(CliIntegrationTests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
# target_link_libraries(CliIntegrationTests PRIVATE Catch2::Catch2WithMain)
# add_dependencies(CliIntegrationTests aosl-compiler)

# Install nothing; tests are run with ctest
