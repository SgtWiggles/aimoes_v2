add_library(pack STATIC
	"src/BitStream.cpp"
	"src/ByteStream.cpp"
    "include/ao/pack/HashingStream.h")
target_include_directories(pack PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> 
    $<INSTALL_INTERFACE:include>
)

add_executable(PackTests
	"tests/BitStreamTests.cpp"
	"tests/ByteStreamTests.cpp"
	"tests/ZigZagTests.cpp"
	"tests/VarintTests.cpp"
)
target_link_libraries(PackTests PRIVATE pack Catch2::Catch2WithMain)

