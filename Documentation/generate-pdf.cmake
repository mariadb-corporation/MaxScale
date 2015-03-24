# The BUILD_DIR variable is set at runtime

cmake_minimum_required(VERSION 2.8.12)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/")
find_package(Pandoc)

if(PANDOC_FOUND AND BUILD_DIR)
  file(MAKE_DIRECTORY ${BUILD_DIR}/pdf)
  file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/makepdf.sh DESTINATION ${BUILD_DIR})
  execute_process(COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Documentation ${BUILD_DIR})
  file(GLOB_RECURSE MARKDOWN ${CMAKE_CURRENT_BINARY_DIR}/*.md)

  foreach(VAR ${MARKDOWN})
    execute_process(COMMAND ${BUILD_DIR}/makepdf.sh ${VAR})
    execute_process(COMMAND ${CMAKE_COMMAND} -E echo ${VAR})
  endforeach()

  file(GLOB PDF ${BUILD_DIR}/Documentation/*.pdf)

  foreach(FILE ${PDF})
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy ${FILE} ${BUILD_DIR}/pdf/)
  endforeach()

endif()
