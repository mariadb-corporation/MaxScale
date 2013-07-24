project (test)
cmake_minimum_required (VERSION 2.6)

# --------- Find crypt
FIND_LIBRARY(LIB_CRYPTO crypto /opt/local/lib /opt/lib /usr/lib /usr/local/lib)
LINK_DIRECTORIES(${Boost_LIBRARY_DIRS})
INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIR})

# Find MySQL client library and header files
find_library(MySQL_LIBRARY NAMES libmysqld.a PATHS
/usr/lib64/mysql /usr/lib/mysql /usr/local/mysql/lib)
find_path(MySQL_INCLUDE_DIR mysql.h
  /usr/local/include/mysql /usr/include/mysql)
include_directories(${MySQL_INCLUDE_DIR})

#SkySQL
find_path(SkySQL_INCLUDE_DIR skygw_debug.h
  /usr/local/include /usr/include ../../utils)
include_directories(${SkySQL_INCLUDE_DIR})

find_path(TRC_INCLUDE_DIR table_replication_consistency.h
 ../ /usr/include /usr/local/include)
include_directories(${TRC_INCLUDE_DIR})

# Build rule for example
foreach(prog Example)
  ADD_EXECUTABLE(${prog} ${prog}.c ../../utils/skygw_utils.o )
  TARGET_LINK_LIBRARIES(${prog} table_replication_consistency.a replication boost_system pthread stdc++ ${MySQL_LIBRARY} crypt aio log_manager)
endforeach()

foreach(prog test)
  ADD_EXECUTABLE(${prog} ${prog}.cpp ../../utils/skygw_utils.o )
  TARGET_LINK_LIBRARIES(${prog} table_replication_consistency.a replication boost_system pthread stdc++ ${MySQL_LIBRARY} crypt aio log_manager)
endforeach()
