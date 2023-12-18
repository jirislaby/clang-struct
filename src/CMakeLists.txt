set(LLVM_LINK_COMPONENTS core support)

#add_definitions(${LLVM_DEFINITIONS})
#include_directories(${LLVM_INCLUDE_DIRS})

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

set(LLVM_OPTIONAL_SOURCES db_filler.cpp server.cpp sqlconn.cpp)

add_llvm_library(clang-struct MODULE
	clang-struct.cpp
	Message.h
	)
