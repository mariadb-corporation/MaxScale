if(RABBITMQ_FOUND AND MYSQLCLIENT_FOUND)

  include_directories(${MYSQLCLIENT_HEADERS})
  add_executable (consumer consumer.c)

  if(MYSQLCLIENT_FOUND)
	target_link_libraries(consumer ${MYSQLCLIENT_LIBRARIES} rabbitmq inih)
  elseif(MYSQLCLIENT_STATIC_FOUND)
	target_link_libraries(consumer ${MYSQLCLIENT_STATIC_LIBRARIES} rabbitmq inih)
  endif()

  install(TARGETS consumer DESTINATION bin)
  install(FILES consumer.cnf DESTINATION etc)

else()

  message(FATAL_ERROR "Error: Can not find requred libraries and headers: librabbitmq libmysqlclient")

endif()
