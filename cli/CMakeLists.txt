add_executable(aosl-compiler
	"src/main.cpp"
)
target_link_libraries(aosl-compiler PUBLIC compiler Boost::program_options)

function(aosl_generate TARGET_NAME OUT_DIR)
    set(options)
    set(oneValArgs FILE_ROOT_DIR)
    set(multiValArgs INPUTS)
    cmake_parse_arguments(AG "${options}" "${oneValArgs}" "${multiValArgs}" ${ARGN})

    cmake_path(ABSOLUTE_PATH OUT_DIR NORMALIZE OUTPUT_VARIABLE OUT_DIR_ABS)
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

    set(OUT_H  "${OUT_DIR_ABS}/${TARGET_NAME}_messages.h")
    set(OUT_CPP "${OUT_DIR_ABS}/${TARGET_NAME}_messages.cpp")
    set(OUT_IR "${OUT_DIR_ABS}/${TARGET_NAME}_messages.aoir")

    add_custom_command(
        OUTPUT "${OUT_H}" "${OUT_IR}" "${OUT_IR}.h"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${OUT_DIR_ABS}"
        COMMAND "$<TARGET_FILE:aosl-compiler>"
            "--include=${FILE_ROOT_DIR_ABS}"
            "--out-header=${OUT_H}"
            "--out-cpp=${OUT_CPP}"
            "--out-ir=${OUT_IR}"
            ${INPUTS_ABS}
        DEPENDS
            aosl-compiler
            ${INPUTS_ABS}
        VERBATIM
        COMMENT "Generating AOSL outputs in ${OUT_DIR_ABS}"
    )

    # target_sources(${TARGET_NAME} PRIVATE "${OUT_H}" "${OUT_CPP}" "${OUT_IR}" "${OUT_IR}.h")
    target_include_directories(${TARGET_NAME} PRIVATE "${OUT_DIR_ABS}")
endfunction()

add_subdirectory(tests)
