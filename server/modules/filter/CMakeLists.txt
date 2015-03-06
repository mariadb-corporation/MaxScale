if(BUILD_RABBITMQ)
  find_package(RabbitMQ)
  include_directories(${RABBITMQ_HEADERS})
  add_library(mqfilter SHARED mqfilter.c)
  target_link_libraries(mqfilter query_classifier log_manager utils ${RABBITMQ_LIBRARIES})
  install(TARGETS mqfilter DESTINATION modules)
endif()

if(BUILD_REDIS)
  find_package(HiRedis)
  include_directories(${HIREDIS_HEADERS})
  add_library(redisfilter SHARED redisfilter.c)
  target_link_libraries(redisfilter query_classifier log_manager utils ${HIREDIS_LIBRARIES})
  install(TARGETS redisfilter DESTINATION modules)
endif()

if(BUILD_ZMQ)
  find_package(Zmq)
  find_package(CZmq)
  include_directories(${CZMQ_HEADERS})
  add_library(zmqfilter SHARED zmqfilter.c)
  target_link_libraries(zmqfilter query_classifier log_manager utils ${ZMQ_LIBRARIES} ${CZMQ_LIBRARIES})
  install(TARGETS zmqfilter DESTINATION modules)
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

add_library(fwfilter SHARED fwfilter.c)
target_link_libraries(fwfilter log_manager utils query_classifier)
install(TARGETS fwfilter DESTINATION modules)

add_library(namedserverfilter SHARED namedserverfilter.c)
target_link_libraries(namedserverfilter log_manager utils)
install(TARGETS namedserverfilter DESTINATION modules)

add_library(lagfilter SHARED lagfilter.c)
target_link_libraries(lagfilter log_manager utils query_classifier)
install(TARGETS lagfilter DESTINATION modules)


add_subdirectory(hint)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
