set(LLVM_LINK_COMPONENTS core support)

add_llvm_library(clang-struct MODULE
	clang-struct.cpp
	../Message.h
	)

add_llvm_library(clang-struct-sa MODULE
	clang-struct.cpp
	../sqlconn.cpp
	../Message.h
	LINK_LIBS ${SQLITE3_LIBRARIES}
	)

target_compile_definitions(clang-struct-sa PUBLIC STANDALONE)
