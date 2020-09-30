#
# Builds Mongo cxx Driver: http://mongocxx.org/mongocxx-v3
#
# The following relevant variables are set:
#
# BSON_CXX_INCLUDE_DIR  - The bson C++ include directory.
# BSON_CXX_ LIBRARIES   - The libraries for the bsoncxx library.
#
# MONGO_CXX_INCLUDE_DIR - The mongo C++ include directory.
# MONGO_CXX_LIBRARIES   - The libraries for the mongo library.
#

set(MONGO_CXX_DRIVER_VERSION "3.6.0")

message(STATUS "Using mongo-cxx-driver version ${MONGO_CXX_DRIVER_VERSION}")

set(MONGO_CXX_DRIVER_URL "https://github.com/mongodb/mongo-cxx-driver/releases/download/r${MONGO_CXX_DRIVER_VERSION}/mongo-cxx-driver-r${MONGO_CXX_DRIVER_VERSION}.tar.gz")

set(MONGO_CXX_DRIVER "${CMAKE_BINARY_DIR}/mongo-cxx-driver")

set(MONGO_CXX_DRIVER_SOURCE  "${MONGO_CXX_DRIVER}/src")
set(MONGO_CXX_DRIVER_BINARY  "${MONGO_CXX_DRIVER}/build")
set(MONGO_CXX_DRIVER_INSTALL "${MONGO_CXX_DRIVER}/install")

ExternalProject_Add(mongo-cxx-driver
  URL ${MONGO_CXX_DRIVER_URL}
  SOURCE_DIR ${MONGO_CXX_DRIVER_SOURCE}
  CMAKE_ARGS -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_INSTALL_PREFIX=${MONGO_CXX_DRIVER_INSTALL} -DCMAKE_PREFIX_PATH=${MONGO_C_DRIVER_INSTALL} -DCMAKE_C_FLAGS=-fPIC  -DCMAKE_CXX_FLAGS=-fPIC -DBUILD_SHARED_AND_STATIC_LIBS=ON
  BINARY_DIR ${MONGO_CXX_DRIVER_BINARY}
  BUILD_COMMAND make
  INSTALL_COMMAND make install
  LOG_DOWNLOAD 1
  LOG_UPDATE 1
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1)

add_dependencies(mongo-cxx-driver mongo-c-driver)

set(BSON_CXX_INCLUDE_DIR  ${MONGO_CXX_DRIVER_INSTALL}/include/bsoncxx/v_noabi CACHE_INTERNAL "")
set(BSON_CXX_LIBRARIES    ${MONGO_CXX_DRIVER_INSTALL}/lib/libbsoncxx-static.a)

set(MONGO_CXX_INCLUDE_DIR ${MONGO_CXX_DRIVER_INSTALL}/include/mongocxx/v_noabi CACHE INTERNAL "")
set(MONGO_CXX_LIBRARIES   ${MONGO_CXX_DRIVER_INSTALL}/lib/libmongocxx-static.a)
