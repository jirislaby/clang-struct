cmake_minimum_required(VERSION 3.3.0)

# C needed for LLVM -> terminfo
project(clang-struct VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 14)
add_compile_options(-Wall)

if (USE_WEXTRA)
	add_compile_options(-Wextra -pedantic -Wno-unused-parameter)
endif()
if (USE_WERROR)
	add_compile_options(-Werror)
endif()

find_package(LLVM REQUIRED CONFIG)

include(AddLLVM)
#find_package(Clang REQUIRED)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

find_package(PkgConfig REQUIRED)

pkg_check_modules(SQLITE3 REQUIRED sqlite3)

add_subdirectory(src)
add_subdirectory(test)
