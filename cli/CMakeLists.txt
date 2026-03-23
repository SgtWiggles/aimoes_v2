add_executable(aosl-compiler
	"src/main.cpp"
)
target_link_libraries(aosl-compiler PUBLIC compiler Boost::program_options)

function(aosl_generate TARGET_NAME)
    set(options)
    set(oneValArgs NAMESPACE FILE_ROOT_DIR BINARY_OUT)
    set(multiValArgs INPUTS)
    cmake_parse_arguments(AG "${options}" "${oneValArgs}" "${multiValArgs}" ${ARGN})

    cmake_path(ABSOLUTE_PATH AG_BINARY_OUT NORMALIZE OUTPUT_VARIABLE OUT_DIR_ABS)
    cmake_path(ABSOLUTE_PATH AG_FILE_ROOT_DIR NORMALIZE OUTPUT_VARIABLE FILE_ROOT_DIR_ABS)

    set(INPUTS_ABS)
    foreach(SRC IN LISTS AG_INPUTS)
        cmake_path(ABSOLUTE_PATH SRC NORMALIZE OUTPUT_VARIABLE SRC_ABS)
        message(STATUS "aosl input = ${SRC_ABS}")
        if(NOT EXISTS "${SRC_ABS}")
            message(FATAL_ERROR "Missing AOSL input: ${SRC_ABS}")
        endif()
        list(APPEND INPUTS_ABS "${SRC_ABS}")
    endforeach()

    set(OUT_ROOT "${OUT_DIR_ABS}/${AG_NAMESPACE}")
    message(STATUS "Aosl output root directory ${OUT_ROOT}")

    set(OUT_FILES
        "${OUT_ROOT}/${TARGET_NAME}_messages.h"
        "${OUT_ROOT}/${TARGET_NAME}_messages.cpp"
        "${OUT_ROOT}/${TARGET_NAME}_messages.aoir"
        "${OUT_ROOT}/${TARGET_NAME}_messages.aoir.h"
    )

    add_custom_command(
        OUTPUT ${OUT_FILES}
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${OUT_ROOT}"
        COMMAND "$<TARGET_FILE:aosl-compiler>"
            "--include=${FILE_ROOT_DIR_ABS}"
            "--out-root=${OUT_DIR_ABS}"
            "--out-dir=${AG_NAMESPACE}"
            "--project=${TARGET_NAME}_messages"
            ${INPUTS_ABS}
        DEPENDS
            aosl-compiler
            ${INPUTS_ABS}
        VERBATIM
        USES_TERMINAL
        COMMENT "Generating AOSL outputs in ${OUT_ROOT}"
    )

    target_sources(${TARGET_NAME} PRIVATE "${OUT_FILES}")
    target_include_directories(${TARGET_NAME} PRIVATE "${OUT_DIR_ABS}")
endfunction()


add_subdirectory(tests)
