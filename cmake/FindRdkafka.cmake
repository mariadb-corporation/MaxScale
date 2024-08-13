#
# Finds librdkafka: https://github.com/edenhill/librdkafka
#
# Following variables are set:
# RDKAFKA_INCLUDE_DIR - Path to librdkafka headers
# RDKAFKA_LIBRARIES   - Path to the librdkafka libraries
#

find_path(RDKAFKA_INCLUDE_DIR rdkafka.h PATH_SUFFIXES librdkafka)
find_library(RDKAFKA_C_LIBRARIES rdkafka)
find_library(RDKAFKA_CXX_LIBRARIES rdkafka++)

if (RDKAFKA_INCLUDE_DIR AND RDKAFKA_C_LIBRARIES AND RDKAFKA_CXX_LIBRARIES)
  set(RDKAFKA_LIBRARIES ${RDKAFKA_CXX_LIBRARIES} ${RDKAFKA_C_LIBRARIES} sasl2)
  message(STATUS "Found librdkafka: ${RDKAFKA_LIBRARIES}")
  set(RDKAFKA_FOUND TRUE)
else()
  message(STATUS "Could not find librdkafka")
endif()

mark_as_advanced(RDKAFKA_C_LIBRARIES RDKAFKA_CXX_LIBRARIES)

