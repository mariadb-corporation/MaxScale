aux_source_directory(${CMAKE_SOURCE_DIR}/server/core CORE_ALL)
foreach(VAR ${CORE_ALL})
  if(NOT( (${VAR} MATCHES "max[a-z_]*.c") OR (${VAR} MATCHES "gateway.c")))
	list(APPEND CORE ${VAR})
  endif()
endforeach()

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_executable(harness_ui harness_ui.c harness_common.c)
add_executable(harness harness_util.c harness_common.c ${CORE})
target_link_libraries(harness_ui fullcore log_manager utils)
target_link_libraries(harness fullcore)
execute_process(COMMAND ${CMAKE_COMMAND} -E copy ${ERRMSG} ${CMAKE_CURRENT_BINARY_DIR})
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/fwtest.cnf.in ${CMAKE_CURRENT_BINARY_DIR}/fwfilter.cnf)



add_test(TestHintfilter /bin/sh -c "MAXSCALE_HOME=\"${CMAKE_BINARY_DIR}\" ${CMAKE_CURRENT_BINARY_DIR}/harness -i ${CMAKE_CURRENT_SOURCE_DIR}/hint_testing.input -o ${CMAKE_CURRENT_BINARY_DIR}/hint_testing.output -c ${CMAKE_CURRENT_SOURCE_DIR}/hint_testing.cnf -t 1 -s 1 -e ${CMAKE_CURRENT_SOURCE_DIR}/hint_testing.expected ")

add_test(TestRegexfilter /bin/sh -c "MAXSCALE_HOME=\"${CMAKE_BINARY_DIR}\" ${CMAKE_CURRENT_BINARY_DIR}/harness -i ${CMAKE_CURRENT_SOURCE_DIR}/regextest.input -o ${CMAKE_CURRENT_BINARY_DIR}/regextest.output -c ${CMAKE_CURRENT_SOURCE_DIR}/regextest.cnf -t 1 -s 1 -e ${CMAKE_CURRENT_SOURCE_DIR}/regextest.expected ")

add_test(TestFwfilter /bin/sh -c "MAXSCALE_HOME=\"${CMAKE_BINARY_DIR}\" ${CMAKE_CURRENT_BINARY_DIR}/harness -i ${CMAKE_CURRENT_SOURCE_DIR}/fwtest.input -o ${CMAKE_CURRENT_BINARY_DIR}/fwtest.output -c ${CMAKE_CURRENT_BINARY_DIR}/fwfilter.cnf -t 1 -s 1 -e ${CMAKE_CURRENT_SOURCE_DIR}/fwtest.expected ")

add_test(TestFwfilter2 /bin/sh -c "MAXSCALE_HOME=\"${CMAKE_BINARY_DIR}\" ${CMAKE_CURRENT_BINARY_DIR}/harness -i ${CMAKE_CURRENT_SOURCE_DIR}/fwtest2.input -o ${CMAKE_CURRENT_BINARY_DIR}/fwtest2.output -c ${CMAKE_CURRENT_BINARY_DIR}/fwfilter.cnf -t 1 -s 1 -e ${CMAKE_CURRENT_SOURCE_DIR}/fwtest2.expected ")