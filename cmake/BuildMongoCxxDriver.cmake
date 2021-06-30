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
  CMAKE_ARGS -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_INSTALL_PREFIX=${MONGO_CXX_DRIVER_INSTALL} -DCMAKE_PREFIX_PATH=${MONGO_C_DRIVER_INSTALL} -DCMAKE_C_FLAGS=-fPIC  -DCMAKE_CXX_FLAGS=-fPIC -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_SHARED_AND_STATIC_LIBS=OFF
  BINARY_DIR ${MONGO_CXX_DRIVER_BINARY}
  PATCH_COMMAND sed -i s/add_subdirectory\(test\)/\#add_subdirectory\(test\)/ src/bsoncxx/CMakeLists.txt && sed -i s/add_subdirectory\(test\)/\#add_subdirectory\(test\)/ src/mongocxx/CMakeLists.txt
  BUILD_COMMAND make
  INSTALL_COMMAND make install
  LOG_DOWNLOAD 1
  LOG_UPDATE 1
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1)

add_dependencies(mongo-cxx-driver mongo-c-driver)

set(BSON_CXX_INCLUDE_DIR  ${MONGO_CXX_DRIVER_INSTALL}/include/bsoncxx/v_noabi CACHE_INTERNAL "")
#Depending on OS it's either lib or lib64, so for the time being taken from the build directory.
#set(BSON_CXX_LIBRARIES    ${MONGO_CXX_DRIVER_INSTALL}/lib/libbsoncxx-static.a)
set(BSON_CXX_LIBRARIES    ${MONGO_CXX_DRIVER_BINARY}/src/bsoncxx/libbsoncxx-static.a)

set(MONGO_CXX_INCLUDE_DIR ${MONGO_CXX_DRIVER_INSTALL}/include/mongocxx/v_noabi CACHE INTERNAL "")
#Depending on OS it's either lib or lib64, so for the time being taken from the build directory.
#set(MONGO_CXX_LIBRARIES   ${MONGO_CXX_DRIVER_INSTALL}/lib/libmongocxx-static.a)
set(MONGO_CXX_LIBRARIES   ${MONGO_CXX_DRIVER_BINARY}/src/mongocxx/libmongocxx-static.a)
