list(APPEND test_files
	nested_struct.c
	packed.c
)

set(LLVM_OPTIONAL_SOURCES ${test_files}
	macro-cond.h
	macro-cond1.c
	macro-cond2.c
	macro-expand.c
	trial.h
	trial.c
)

foreach(test_file IN LISTS test_files)
	add_test(NAME ${test_file}
		COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_test.sh ${CMAKE_CURRENT_SOURCE_DIR}/${test_file})
endforeach()
