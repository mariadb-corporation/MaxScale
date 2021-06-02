if(BUILD_CDC)
  add_subdirectory(CDC)
  add_subdirectory(examples)
endif()

add_subdirectory(MariaDB)

if(BUILD_NOSQL)
  add_subdirectory(NoSQL)
endif()
