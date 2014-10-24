project (skysql_m_table_replication_concistency)
cmake_minimum_required (VERSION 2.6)

# This configuration file builds both the static and shared version of
# the library.

set(table_replication_consistency_sources table_replication_consistency.cpp table_replication_listener.cpp table_replication_parser.cpp table_replication_metadata.cpp)

# ---------- Find Boost Headers/Libraries -----------------------
SET(Boost_DEBUG FALSE)
SET(Boost_FIND_REQUIRED TRUE)
SET(Boost_FIND_QUIETLY TRUE)
SET(Boost_USE_STATIC_LIBS FALSE)
SET(Boost_ADDITIONAL_VERSIONS "1.41" "1.41.0")
FIND_PACKAGE(Boost REQUIRED system thread)

# --------- Find crypt
FIND_LIBRARY(LIB_CRYPTO crypto /opt/local/lib /opt/lib /usr/lib /usr/local/lib /usr/local/ssl/lib)
LINK_DIRECTORIES(${Boost_LIBRARY_DIRS})
INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIR})

# Find MySQL client library and header files
find_library(MySQL_LIBRARY NAMES libmysqld.a PATHS
/usr/lib64/mysql /usr/lib/mysql /usr/local/mysql/lib)
find_path(MySQL_INCLUDE_DIR mysql.h
  /usr/local/include/mysql /usr/include/mysql /usr/local/mysql/include)
include_directories(${MySQL_INCLUDE_DIR})

#MariaDB Corporation
find_path(MariaDB_Corporation_INCLUDE_DIR skygw_debug.h
  /usr/local/include /usr/include ../utils)
include_directories(${MariaDB_Corporation_INCLUDE_DIR})
include_directories(../replication_listener)

#log_manager
FIND_LIBRARY(LIB_LOGMANAGER log_manager /lib /opt/local/lib /opt/lib /usr/lib /usr/local/lib ../log_manager)
find_path(LogManager_INCLUDE_DIR log_manager.h
 /usr/local/include /usr/include ../log_manager)
include_directories(${LogManager_INCLUDE_DIR})

#debug settings
SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DTBR_DEBUG")
SET(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DTBR_DEBUG")

# Configure for building static library
add_library(table_replication_consistency_static STATIC ${table_replication_consistency_sources})
target_link_libraries(table_replication_consistency_static crypto log_manager ${Boost_LIBRARIES} ${MySQL_LIBRARY})
set_target_properties(table_replication_consistency_static PROPERTIES
  OUTPUT_NAME "table_replication_consistency")

# Configure for building shared library
add_library(table_replication_consistency_shared SHARED ${table_replication_consistency_sources})
target_link_libraries(table_replication_consistency_shared crypto log_manager ${Boost_LIBRARIES} ${MySQL_LIBRARY})

set_target_properties(table_replication_consistency_shared PROPERTIES
  VERSION 0.1 SOVERSION 1
  OUTPUT_NAME "table_replication_consistency")

install(TARGETS table_replication_consistency_shared LIBRARY DESTINATION lib)
install(TARGETS table_replication_consistency_static ARCHIVE DESTINATION lib)

