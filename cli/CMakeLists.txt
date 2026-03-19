add_executable(aosl-compiler
	"src/main.cpp"
)

target_link_libraries(aosl-compiler PUBLIC compiler Boost::program_options)
