# The BUILD_DIR variable is set at runtime

cmake_minimum_required(VERSION 2.8.12)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

file(MAKE_DIRECTORY ${BUILD_DIR}/txt)
file(GLOB_RECURSE MARKDOWN Release-Notes/*.md)
foreach(VAR ${MARKDOWN})
  get_filename_component(NEWNAME ${VAR} NAME)
  execute_process(COMMAND perl ${CMAKE_CURRENT_BINARY_DIR}/format.pl ${VAR} ${BUILD_DIR}/txt/${NEWNAME}.txt)
endforeach()
