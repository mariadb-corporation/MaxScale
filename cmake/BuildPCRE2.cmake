# Build the PCRE2 library from source
#
# This will add a 'pcre2' target to CMake which will generate the libpcre2-8.so
# dynamic library and the pcre2.h header. If your target requires PCRE2 you
# need to add a dependeny on the 'pcre2' target by adding add_dependencies(<target> pcre2)
# to the CMakeLists.txt

set(PCRE_ROOT_DIR ${CMAKE_SOURCE_DIR}/pcre2/)
set(PCRE_BUILD_DIR ${CMAKE_BINARY_DIR}/pcre2/)
set(PCRE2_LIBRARIES ${CMAKE_BINARY_DIR}/pcre2/libpcre2-8.so CACHE STRING "PCRE2 dynamic libraries" FORCE)
execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${PCRE_BUILD_DIR})
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_directory ${PCRE_ROOT_DIR} ${PCRE_BUILD_DIR})

add_custom_target(pcre2 COMMAND ${CMAKE_COMMAND} ${PCRE_BUILD_DIR}
  -DBUILD_SHARED_LIBS=Y
  -DPCRE2_BUILD_PCRE2GREP=N
  -DPCRE2_BUILD_TESTS=N
  COMMAND make
  WORKING_DIRECTORY ${PCRE_BUILD_DIR})

include_directories(${CMAKE_BINARY_DIR}/pcre2/)
install(PROGRAMS ${PCRE2_LIBRARIES} DESTINATION ${MAXSCALE_LIBDIR})
