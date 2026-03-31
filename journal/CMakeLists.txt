add_library(journal STATIC
	"include/ao/journal/Journal.h"
	"src/Journal.cpp"
)
target_include_directories(journal PUBLIC include)
target_link_libraries(journal PUBLIC compiler)


add_executable(JournalTests "tests/JournalTests.cpp")
target_link_libraries(JournalTests PRIVATE journal Catch2::Catch2WithMain)
