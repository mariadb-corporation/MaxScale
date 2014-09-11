if(BUILD_RABBITMQ)
  add_library(mqfilter SHARED mqfilter.c)
  set_target_properties(mqfilter PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${CMAKE_INSTALL_PREFIX}/lib)
  target_link_libraries(mqfilter utils query_classifier log_manager rabbitmq)
  install(TARGETS  DESTINATION modules)
endif(BUILD_RABBITMQ)

add_library(regexfilter SHARED regexfilter.c)
set_target_properties(regexfilter PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${CMAKE_INSTALL_PREFIX}/lib)
target_link_libraries(regexfilter utils log_manager)
install(TARGETS regexfilter DESTINATION modules)

add_library(testfilter SHARED testfilter.c)
set_target_properties(testfilter PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${CMAKE_INSTALL_PREFIX}/lib)
target_link_libraries(testfilter utils log_manager)
install(TARGETS testfilter DESTINATION modules)

add_library(qlafilter SHARED qlafilter.c)
set_target_properties(qlafilter PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${CMAKE_INSTALL_PREFIX}/lib)
target_link_libraries(qlafilter utils log_manager)
install(TARGETS qlafilter DESTINATION modules)

add_library(tee SHARED tee.c)
set_target_properties(tee PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${CMAKE_INSTALL_PREFIX}/lib)
target_link_libraries(tee utils log_manager)
install(TARGETS tee DESTINATION modules)

add_library(topfilter SHARED topfilter.c)
set_target_properties(topfilter PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${CMAKE_INSTALL_PREFIX}/lib)
target_link_libraries(topfilter utils log_manager)
install(TARGETS topfilter DESTINATION modules)

add_subdirectory(hint)