if(BUILD_RABBITMQ)
  if(RABBITMQ_LIB AND RABBITMQ_HEADERS)
    add_library(mqfilter SHARED mqfilter.c)
    target_link_libraries(mqfilter utils query_classifier log_manager rabbitmq)
    install(TARGETS mqfilter DESTINATION modules)
  else()
    message(ERROR "Error: Cannot find the required librabbitmq-c locations, please check that you have them configured correctly.")
  endif()
endif()

add_library(regexfilter SHARED regexfilter.c)
target_link_libraries(regexfilter utils log_manager)
install(TARGETS regexfilter DESTINATION modules)

add_library(testfilter SHARED testfilter.c)
target_link_libraries(testfilter utils log_manager)
install(TARGETS testfilter DESTINATION modules)

add_library(qlafilter SHARED qlafilter.c)
target_link_libraries(qlafilter utils log_manager)
install(TARGETS qlafilter DESTINATION modules)

add_library(tee SHARED tee.c)
target_link_libraries(tee utils log_manager)
install(TARGETS tee DESTINATION modules)

add_library(topfilter SHARED topfilter.c)
target_link_libraries(topfilter utils log_manager)
install(TARGETS topfilter DESTINATION modules)

add_subdirectory(hint)