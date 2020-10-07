if(BUILD_CDC)
  add_subdirectory(CDC)
  add_subdirectory(examples)
endif()

add_subdirectory(MariaDB)

if(BUILD_MONGO)
  add_subdirectory(MongoDB)
endif()
