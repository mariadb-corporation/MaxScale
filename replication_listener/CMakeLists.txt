project (mariadb-replication-listener-api)
cmake_minimum_required(VERSION 2.6)

# This configuration file builds both the static and shared version of
# the library.
set(replication_sources
  access_method_factory.cpp 
  binlog_driver.cpp tcp_driver.cpp basic_content_handler.cpp
  binary_log.cpp protocol.cpp binlog_event.cpp
  gtid.cpp resultset_iterator.cpp value.cpp row_of_fields.cpp)

# Find MySQL client library and header files
find_library(MySQL_LIBRARY NAMES libmysqld.a PATHS
/usr/lib64/mysql /usr/lib/mysql /usr/local/mysql/lib ${MARIADB_SRC_PATH}/lib)

SET(Boost_DEBUG FALSE)
SET(Boost_FIND_REQUIRED TRUE)
SET(Boost_FIND_QUIETLY TRUE)
SET(Boost_USE_STATIC_LIBS FALSE)
SET(Boost_ADDITIONAL_VERSIONS "1.41" "1.41.0")
FIND_PACKAGE(Boost REQUIRED system thread)

FIND_LIBRARY(LIB_CRYPTO NAMES libcrypto.a /opt/local/lib /opt/lib /usr/lib /usr/local/lib /usr/local/ssl/lib)
LINK_DIRECTORIES(${LIB_CRYPTO})
LINK_DIRECTORIES(${Boost_LIBRARY_DIRS})
INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIR})

include_directories(${MARIADB_SRC_PATH})
include_directories(/usr/local/mysql/include)
include_directories(../../utils)
include_directories(.)

# Configure for building static library
add_library(replication_static STATIC ${replication_sources})
target_link_libraries(replication_static ${CYPTO} ${Boost_LIBRARIES} ${MySQL_LIBRARY})
set_target_properties(replication_static PROPERTIES
  OUTPUT_NAME "replication")

# Configure for building shared library
add_library(replication_shared SHARED ${replication_sources})
target_link_libraries(replication_shared ${CYPTO} ${Boost_LIBRARIES} ${MySQL_LIBRARY})

set_target_properties(replication_shared PROPERTIES
  VERSION 0.1 SOVERSION 1
  OUTPUT_NAME "replication")

install(TARGETS replication_shared LIBRARY DESTINATION lib)
install(TARGETS replication_static ARCHIVE DESTINATION lib)
