#
# Builds Mongo C Driver: https://github.com/mongodb/mongo-c-driver
#
# The following relevant variables are set:
#
# BSON_C_INCLUDE_DIR   - The bson include directory.
# BSON_C_LIBRARIES     - The libraries for the bson library.
#
# MONGO_C_INCLUDE_DIR  - The mongo include directory.
# MONGO_C_SRC_DIR      - The mongo source directory.
# MONGO_C_BUILD_DIR    - The mongo build directory where generated files end up.
# MONGO_C_LIBRARIES    - The libraries for the mongo library.
#

set(MONGO_C_DRIVER_VERSION "1.17.0")

message(STATUS "Using mongo-c-driver version ${MONGO_C_DRIVER_VERSION}")

set(MONGO_C_DRIVER_URL "https://github.com/mongodb/mongo-c-driver/releases/download/${MONGO_C_DRIVER_VERSION}/mongo-c-driver-${MONGO_C_DRIVER_VERSION}.tar.gz")

set(MONGO_C_DRIVER "${CMAKE_BINARY_DIR}/mongo-c-driver")

set(MONGO_C_DRIVER_SOURCE  "${MONGO_C_DRIVER}/src")
set(MONGO_C_DRIVER_BINARY  "${MONGO_C_DRIVER}/build")
set(MONGO_C_DRIVER_INSTALL "${MONGO_C_DRIVER}/install")

ExternalProject_Add(mongo-c-driver
  URL ${MONGO_C_DRIVER_URL}
  SOURCE_DIR ${MONGO_C_DRIVER_SOURCE}
  CMAKE_ARGS -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_INSTALL_PREFIX=${MONGO_C_DRIVER_INSTALL}  -DCMAKE_C_FLAGS=-fPIC -DENABLE_AUTOMATIC_INIT_AND_CLEANUP=OFF -DENABLE_TESTS=N -DENABLE_EXAMPLES=N
  BINARY_DIR ${MONGO_C_DRIVER_BINARY}
  BUILD_COMMAND make
  INSTALL_COMMAND make install
  LOG_DOWNLOAD 1
  LOG_UPDATE 1
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1)

set(BSON_C_INCLUDE_DIR  ${MONGO_C_DRIVER_INSTALL}/include/libbson-1.0 CACHE INTERNAL "")
#Depending on OS it's either lib or lib64, so for the time being taken from the build directory.
#set(BSON_C_LIBRARIES    ${MONGO_C_DRIVER_INSTALL}/lib/libbson-static-1.0.a)
set(BSON_C_LIBRARIES    ${MONGO_C_DRIVER_BINARY}/src/libbson/libbson-static-1.0.a)

set(MONGO_C_INCLUDE_DIR ${MONGO_C_DRIVER_INSTALL}/include/libmongoc-1.0 CACHE INTERNAL "")
set(MONGO_C_SRC_DIR     ${MONGO_C_DRIVER_SOURCE}/src/libmongoc/src/mongoc CACHE INTERNAL "")
set(MONGO_C_BUILD_DIR   ${MONGO_C_DRIVER_BINARY}/src/libmongoc/src/mongoc CACHE INTERNAL "")
#Depending on OS it's either lib or lib64, so for the time being taken from the build directory.
#set(MONGO_C_LIBRARIES   ${MONGO_C_DRIVER_INSTALL}/lib64/libmongoc-static-1.0.a)
set(MONGO_C_LIBRARIES   ${MONGO_C_DRIVER_BINARY}/src/libmongoc/libmongoc-static-1.0.a)
