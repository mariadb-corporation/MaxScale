# The BUILD_DIR variable is set at runtime

cmake_minimum_required(VERSION 2.8.12)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/")
find_package(Pandoc)

if(PANDOC_FOUND AND BUILD_DIR)
  file(MAKE_DIRECTORY ${BUILD_DIR}/html)
  file(GLOB_RECURSE MARKDOWN *.md)
  foreach(VAR ${MARKDOWN})
    string(REPLACE ".md" ".html" OUTPUT ${VAR})
    get_filename_component(DIR ${VAR} DIRECTORY)
    string(REPLACE "${CMAKE_CURRENT_BINARY_DIR}" "${BUILD_DIR}/html" FILE ${OUTPUT})
    execute_process(COMMAND ${CMAKE_COMMAND} -E chdir ${DIR} ${PANDOC_EXECUTABLE} ${VAR} -o ${OUTPUT})
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy ${OUTPUT} ${FILE})
  endforeach()
endif()
