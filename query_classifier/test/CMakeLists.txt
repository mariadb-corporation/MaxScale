if(${ERRMSG} MATCHES "NOTFOUND")
  message(FATAL_ERROR "The errmsg.sys file was not found, please define the path with -DERRMSG=<path>")
else()
  file(COPY ${ERRMSG} DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
endif()

add_subdirectory(canonical_tests)
add_executable(classify classify.c)
target_link_libraries(classify query_classifier fullcore)
add_test(NAME TestQueryClassifier
  COMMAND /bin/sh -c "${CMAKE_CURRENT_BINARY_DIR}/classify < ${CMAKE_CURRENT_SOURCE_DIR}/input.sql > ${CMAKE_CURRENT_BINARY_DIR}/output.sql &&
		diff ${CMAKE_CURRENT_BINARY_DIR}/output.sql ${CMAKE_CURRENT_SOURCE_DIR}/expected.sql; 
if [[ $? -eq 1 ]];
	then 
		echo \"TEST FAILED\";
		exit 1;
	else
		echo\"TEST PASSED\";
		exit 0;
fi")