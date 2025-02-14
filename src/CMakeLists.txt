set(LLVM_LINK_COMPONENTS core support)

#add_definitions(${LLVM_DEFINITIONS})
#include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${MARIADB_INCLUDE_DIRS})

add_llvm_library(clang-struct MODULE
	clang-struct.cpp
	sqlconn.cpp
	LINK_LIBS ${MARIADB_LIBRARIES}
)
