set(LLVM_LINK_COMPONENTS core support)

if (NOT ONLY_STANDALONE)
add_llvm_library(clang-struct MODULE
	clang-struct.cpp
	../Message.h
	)
endif()

add_llvm_library(clang-struct-sa MODULE
	clang-struct.cpp
	../sqlconn.cpp
	../Message.h
	LINK_LIBS ${SLSQLITE_LIBRARIES}
	)

target_compile_definitions(clang-struct-sa PUBLIC STANDALONE)
