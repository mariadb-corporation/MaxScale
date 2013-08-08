project(tests)
cmake_minimum_required (VERSION 2.6)

include_directories(../.)
include_directories(../table_replication_consistency)

FIND_LIBRARY(CRYPTO NAMES libcrypto.a /opt/local/lib /opt/lib /usr/lib /usr/local/lib /usr/local/ssl/lib)
FIND_LIBRARY(SSL NAMES libssl.a /opt/local/lib /opt/lib /usr/lib /usr/local/lib /usr/local/ssl/lib)
FIND_LIBRARY(REPLICATION replication /opt/local/lib /opt/lib /usr/lib /usr/local/lib ../)

# Find MySQL client library and header files
find_path(MySQL_INCLUDE_DIR mysql.h
  /usr/local/include/mysql /usr/include/mysql /usr/local/mysql/include)
include_directories(${MySQL_INCLUDE_DIR})
include_directories(../../table_replication_consistency)
include_directories(../../utils)

# Find MySQL client library and header files
find_library(MySQL_LIBRARY NAMES libmysqld.a PATHS
/usr/lib64/mysql /usr/lib/mysql /usr/local/mysql/lib ${MARIADB_SRC_PATH}/lib)

SET(Boost_DEBUG FALSE)
SET(Boost_FIND_REQUIRED TRUE)
SET(Boost_FIND_QUIETLY TRUE)
SET(Boost_USE_STATIC_LIBS FALSE)
SET(Boost_ADDITIONAL_VERSIONS "1.41" "1.41.0")
FIND_PACKAGE(Boost REQUIRED system thread)

# Create build rules for all the simple examples that only require a
# single file.
foreach(prog event_dump)
  ADD_EXECUTABLE(${prog} ${prog}.cpp /usr/local/mysql/lib/libmysqld.a)
  TARGET_LINK_LIBRARIES(${prog} ${REPLICATION} boost_system boost_thread pthread aio ${SSL} ${CRYPTO} crypt z dl ${MySQL_LIBRARY})
endforeach()

