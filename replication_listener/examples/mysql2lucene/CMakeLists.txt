project (mysql2lucene)

find_package(CLucene)

add_executable(mysql2lucene
  main.cpp table_delete.cpp table_index.cpp
  table_insert.cpp table_update.cpp)
include_directories(${CLUCENE_INCLUDE_DIR} ${CLUCENE_LIBRARY_DIR})
target_link_libraries(mysql2lucene ${CLUCENE_LIBRARY} replication_static)
