project (test)
cmake_minimum_required (VERSION 2.6)

# --------- Find crypt
FIND_LIBRARY(CRYPTO NAMES libcrypto.a /opt/local/lib /opt/lib /usr/lib /usr/local/lib /usr/local/ssl/lib)
FIND_LIBRARY(SSL NAMES libssl.a /opt/local/lib /opt/lib /usr/lib /usr/local/lib /usr/local/ssl/lib)
FIND_LIBRARY(REPLICATION replication /opt/local/lib /opt/lib /usr/lib /usr/local/lib ../)

LINK_DIRECTORIES(${Boost_LIBRARY_DIRS})
INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIR})

# Find MySQL client library and header files
find_path(MySQL_INCLUDE_DIR mysql.h
  /usr/local/include/mysql /usr/include/mysql /usr/local/mysql/include)
include_directories(${MySQL_INCLUDE_DIR})

#MariaDB Corporation
find_path(MariaDB_Corporation_INCLUDE_DIR skygw_debug.h
  /usr/local/include /usr/include ../../utils)
include_directories(${MariaDB_Corporation_INCLUDE_DIR})

find_path(TRC_INCLUDE_DIR table_replication_consistency.h
 ../ /usr/include /usr/local/include)
include_directories(${TRC_INCLUDE_DIR})

# Build rule for example
foreach(prog Example)
  ADD_EXECUTABLE(${prog} ${prog}.c ../../utils/skygw_utils.o /usr/local/mysql/lib/libmysqld.a)
  TARGET_LINK_LIBRARIES(${prog} table_replication_consistency.a replication boost_system boost_thread pthread stdc++ ${SSL} ${CRYPTO} crypt z dl aio log_manager)
endforeach()

foreach(prog test)
  ADD_EXECUTABLE(${prog} ${prog}.cpp ../../utils/skygw_utils.o /usr/local/mysql/lib/libmysqld.a)
  TARGET_LINK_LIBRARIES(${prog} table_replication_consistency.a replication boost_system boost_thread pthread stdc++ ${SSL} ${CRYPTO} crypt z dl aio log_manager)
endforeach()
