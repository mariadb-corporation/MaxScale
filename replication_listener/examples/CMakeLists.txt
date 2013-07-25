project(examples)
cmake_minimum_required (VERSION 2.6)

link_directories(${PROJECT_BUILD_DIR}/lib)
include_directories(${PROJECT_BUILD_DIR}/include)

# Find MySQL client library and header files
find_path(MySQL_INCLUDE_DIR mysql.h
  /usr/local/include/mysql /usr/include/mysql)
include_directories(${MySQL_INCLUDE_DIR})
include_directories(../../table_replication_consistency)
include_directories(../../utils)


# Create build rules for all the simple examples that only require a
# single file.
foreach(prog basic-1 basic-2 jan_test)
  ADD_EXECUTABLE(${prog} ${prog}.cpp /usr/local/mysql/lib/libmysqld.a)
  TARGET_LINK_LIBRARIES(${prog} replication boost_system boost_thread pthread aio crypt ${MySQL_LIBRARY})
endforeach()

add_subdirectory(mysql2lucene EXCLUDE_FROM_ALL)
