execute_process(COMMAND /bin/sh -c "${CMAKE_BINARY_DIR}/bin/maxscale -f ${CMAKE_BINARY_DIR}/maxscale.cnf --logdir=${CMAKE_BINARY_DIR}/ --datadir=${CMAKE_BINARY_DIR}/ --cachedir=${CMAKE_BINARY_DIR}/ &> ${CMAKE_BINARY_DIR}/maxscale.output"
OUTPUT_VARIABLE MAXSCALE_OUT)
execute_process(COMMAND make test RESULT_VARIABLE RVAL)
execute_process(COMMAND killall maxscale)
if(NOT RVAL EQUAL 0)
  message(FATAL_ERROR "Test suite failed with status: ${RVAL}")
else()
  message(STATUS "Test exited with status: ${RVAL}")
endif()
