set(LLVM_LINK_COMPONENTS core support)

#add_definitions(${LLVM_DEFINITIONS})
#include_directories(${LLVM_INCLUDE_DIRS})

if (NOT ONLY_STANDALONE)
add_executable(db_filler
	db_filler.cpp
	server.cpp
	server.h
	sqlconn.cpp
	sqlconn.h
	sqlite.h
	Message.h
	)
target_link_libraries(db_filler ${SQLITE3_LIBRARIES})
install(TARGETS db_filler)
endif()

add_subdirectory(clang-struct)
