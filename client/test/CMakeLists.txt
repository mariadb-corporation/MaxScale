install(PROGRAMS maxadmin_test.sh DESTINATION ${CMAKE_BINARY_DIR})
install(PROGRAMS maxadmin_stress.sh DESTINATION ${CMAKE_BINARY_DIR})
add_test(TestMaxAdmin ${CMAKE_BINARY_DIR}/maxadmin_test.sh)
add_test(TestMaxAdminStress ${CMAKE_BINARY_DIR}/maxadmin_stress.sh)
