add_library(pack OBJECT
	"src/BitStream.cpp"
	"src/ByteStream.cpp"
)
target_include_directories(pack PUBLIC include)

add_executable(PackTests
	"tests/BitStreamTests.cpp"
	"tests/ByteStreamTests.cpp"
	"tests/ZigZagTests.cpp"
	"tests/VarintTests.cpp"
)
target_link_libraries(PackTests PRIVATE pack Catch2::Catch2WithMain)

