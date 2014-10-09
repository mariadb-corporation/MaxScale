if(BUILD_RABBITMQ)
  add_library(mqfilter SHARED mqfilter.c)
  target_link_libraries(mqfilter query_classifier log_manager utils rabbitmq)
  install(TARGETS mqfilter DESTINATION modules)
endif()

add_library(regexfilter SHARED regexfilter.c)
target_link_libraries(regexfilter log_manager utils)
install(TARGETS regexfilter DESTINATION modules)

add_library(testfilter SHARED testfilter.c)
target_link_libraries(testfilter log_manager utils)
install(TARGETS testfilter DESTINATION modules)

add_library(qlafilter SHARED qlafilter.c)
target_link_libraries(qlafilter log_manager utils)
install(TARGETS qlafilter DESTINATION modules)

add_library(tee SHARED tee.c)
target_link_libraries(tee log_manager utils)
install(TARGETS tee DESTINATION modules)

add_library(topfilter SHARED topfilter.c)
target_link_libraries(topfilter log_manager utils)
install(TARGETS topfilter DESTINATION modules)

add_subdirectory(hint)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()