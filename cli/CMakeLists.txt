add_executable(aosl-compiler
	"src/main.cpp"
)
target_link_libraries(aosl-compiler PUBLIC compiler Boost::program_options)

# Function to generate sources from .aosl files
function(aosl_generate TARGET_NAME OUT_DIR)
	set(options)
	set(oneValArgs FILE_ROOT_DIR)
	set(multiValArgs INPUTS)
	cmake_parse_arguments(AG "${options}" "${oneValArgs}" "${multiValArgs}" ${ARGN})

	file(MAKE_DIRECTORY "${OUT_DIR}")

	set(GEN_FILES)
	# Build lists of generated files and prepare compiler argument list
	set(COMPILER_ARGS)

	set(OUT_H "${OUT_DIR}/messages.h")
	set(OUT_IR "${OUT_DIR}/messages.aoir")
	list(APPEND COMPILER_ARGS "--include=${AG_FILE_ROOT_DIR}")
	list(APPEND COMPILER_ARGS "--out-header=${OUT_H}")
	list(APPEND COMPILER_ARGS "--out-ir=${OUT_IR}")

	list(APPEND GEN_FILES "${OUT_H}" "${OUT_IR}")

	foreach(SRC ${AG_INPUTS})
		get_filename_component(BNAME ${SRC} NAME_WE)
		# Append per-file arguments to the single compiler invocation
		list(APPEND COMPILER_ARGS "${SRC}")
	endforeach()

	if(GEN_FILES)
		add_custom_command(
		OUTPUT ${GEN_FILES}
		COMMAND ${CMAKE_COMMAND} -E make_directory "${OUT_DIR}"
		# Single invocation of the compiler with all per-file args appended
		COMMAND $<TARGET_FILE:aosl-compiler> ${COMPILER_ARGS}
		DEPENDS ${AG_INPUTS} aosl-compiler
		COMMENT "Generating AOSL outputs in ${OUT_DIR}"
		VERBATIM
	)
		add_custom_target(${TARGET_NAME}_aosl_gen DEPENDS ${GEN_FILES})
		set(GENERATED_${TARGET_NAME} ${GEN_FILES} PARENT_SCOPE)
	else()
		message(WARNING "aosl_generate called with no inputs for ${TARGET_NAME}")
		set(GENERATED_${TARGET_NAME} "" PARENT_SCOPE)
	endif()
endfunction()
