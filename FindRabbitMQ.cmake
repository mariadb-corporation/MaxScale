# This CMake file tries to find the the RabbitMQ library
# The following variables are set:
# RABBITMQ_FOUND - System has RabbitMQ client
# RABBITMQ_LIBRARIES - The RabbitMQ client library
# RABBITMQ_HEADERS - The RabbitMQ client headers
include(CheckCSourceCompiles)
find_library(RABBITMQ_LIBRARIES NAMES rabbitmq)
find_path(RABBITMQ_HEADERS amqp.h PATH_SUFFIXES mysql mariadb)

if(${RABBITMQ_LIBRARIES} MATCHES "NOTFOUND")
  set(RABBITMQ_FOUND FALSE CACHE INTERNAL "")
  message(STATUS "RabbitMQ library not found.")
  unset(RABBITMQ_LIBRARIES)
else()
  set(RABBITMQ_FOUND TRUE CACHE INTERNAL "")
  message(STATUS "Found RabbitMQ library: ${RABBITMQ_LIBRARIES}")
endif()

set(CMAKE_REQUIRED_INCLUDES ${RABBITMQ_HEADERS})
check_c_source_compiles("#include <amqp.h>\n int main(){if(AMQP_DELIVERY_PERSISTENT){return 0;}return 1;}" HAVE_RABBITMQ50)
if(NOT HAVE_RABBITMQ50)
  message(FATAL_ERROR "Old version of RabbitMQ-C library found. Version 0.5 or newer is required.")
endif()