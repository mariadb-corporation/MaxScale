file(COPY MaxScale_test.cnf DESTINATION ${CMAKE_BINARY_DIR}/etc)
file(RENAME ${CMAKE_BINARY_DIR}/etc/MaxScale_test.cnf ${CMAKE_BINARY_DIR}/etc/MaxScale.cnf)
add_test(NAME RunExecutable COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/startmaxscale.sh "$<TARGET_FILE_DIR:maxscale>" "-c ${CMAKE_BINARY_DIR}")
set_tests_properties(RunExecutable PROPERTIES TIMEOUT 5)
set_tests_properties(RunExecutable PROPERTIES WILL_FAIL TRUE)

add_test(NAME KillExecutable COMMAND killall -KILL maxscale)
set_tests_properties(KillExecutable PROPERTIES COST 0.00001)
