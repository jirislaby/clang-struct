cmake_minimum_required(VERSION 3.10.0)

# C needed for LLVM -> terminfo
project(clang-struct VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
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

find_package(cxxopts REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SLSQLITE REQUIRED slsqlite++)
pkg_check_modules(SLHELPERS REQUIRED slhelpers++)

enable_testing()

add_subdirectory(scripts)
add_subdirectory(src)
add_subdirectory(test)
