list(APPEND test_files
	nested_struct.c
	packed.c
)

set(LLVM_OPTIONAL_SOURCES ${test_files} trial.c trial.h)

foreach(test_file IN LISTS test_files)
	add_test(NAME ${test_file}
		COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_test.sh ${CMAKE_CURRENT_SOURCE_DIR}/${test_file})
endforeach()
