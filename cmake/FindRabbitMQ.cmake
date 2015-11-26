# This CMake file tries to find the the RabbitMQ 0.5 library
# The following variables are set:
# RABBITMQ_FOUND - System has RabbitMQ client
# RABBITMQ_LIBRARIES - The RabbitMQ client library
# RABBITMQ_HEADERS - The RabbitMQ client headers

include(CheckCSourceCompiles)
find_library(RABBITMQ_LIBRARIES NAMES rabbitmq)
find_path(RABBITMQ_HEADERS amqp.h)

if(${RABBITMQ_LIBRARIES} MATCHES "NOTFOUND")
  set(RABBITMQ_FOUND FALSE CACHE INTERNAL "")
  message(STATUS "RabbitMQ library not found.")
  unset(RABBITMQ_LIBRARIES)
else()
  set(RABBITMQ_FOUND TRUE CACHE INTERNAL "")
endif()

if(RABBITMQ_FOUND)
  set(CMAKE_REQUIRED_INCLUDES ${RABBITMQ_HEADERS})

  check_c_source_compiles("#include <amqp.h>\n int main(){if(AMQP_DELIVERY_PERSISTENT){return 0;}return 1;}" HAVE_RABBITMQ50)

  if(HAVE_RABBITMQ50)
    execute_process(COMMAND grep "#define *AMQP_VERSION_MAJOR" "${RABBITMQ_HEADERS}/amqp.h"
      COMMAND sed -e "s/.* //"
      OUTPUT_VARIABLE AMQP_VERSION_MAJOR
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND grep "#define *AMQP_VERSION_MINOR" "${RABBITMQ_HEADERS}/amqp.h"
      COMMAND sed -e "s/.* //"
      OUTPUT_VARIABLE AMQP_VERSION_MINOR
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND grep  "#define *AMQP_VERSION_PATCH" "${RABBITMQ_HEADERS}/amqp.h"
      COMMAND sed -e "s/.* //"
      OUTPUT_VARIABLE AMQP_VERSION_PATCH
      OUTPUT_STRIP_TRAILING_WHITESPACE)

    set(AMQP_VERSION "${AMQP_VERSION_MAJOR}.${AMQP_VERSION_MINOR}.${AMQP_VERSION_PATCH}")

    if(NOT "${AMQP_VERSION}" VERSION_LESS "0.6.0")
      add_definitions(-DRABBITMQ_060)
    endif()

    message(STATUS "Found RabbitMQ version ${AMQP_VERSION}: ${RABBITMQ_LIBRARIES}")
    message(STATUS "Found RabbitMQ development headers at: ${RABBITMQ_HEADERS}")

  else()
    message(WARNING "RabbitMQ-C library does not have AMQP_DELIVERY_PERSISTENT. Version 0.5 or newer is required but version ${AMQP_VERSION} was found.")
  endif()

endif()
