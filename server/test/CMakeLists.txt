file(COPY MaxScale_test.cnf DESTINATION ${CMAKE_BINARY_DIR}/etc)
file(RENAME ${CMAKE_BINARY_DIR}/etc/MaxScale_test.cnf ${CMAKE_BINARY_DIR}/etc/MaxScale.cnf)
add_test(NAME RunExecutable COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/startmaxscale.sh "${CMAKE_BINARY_DIR}/bin/" "-c ${CMAKE_BINARY_DIR}")
set_tests_properties(RunExecutable PROPERTIES TIMEOUT 2)

add_test(NAME KillExecutable COMMAND killall -KILL maxscale)
set_tests_properties(KillExecutable PROPERTIES DEPENDS StackHintTest ) #this needs to be the last test that requires a running maxscale
