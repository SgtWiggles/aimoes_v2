add_library(journal STATIC)
target_include_directories(journal PUBLIC include)
target_link_libraries(journal PUBLIC compiler)
