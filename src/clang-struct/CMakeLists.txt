set(LLVM_LINK_COMPONENTS core support)

add_llvm_library(clang-struct MODULE
	clang-struct.cpp
	../Message.h
	)
