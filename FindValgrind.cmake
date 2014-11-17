# This CMake file tries to find the Valgrind executable
# The following variables are set:
# VALGRIND_FOUND - System has Valgrind
# VALGRIND_EXECUTABLE - The Valgrind executable file
find_program(VALGRIND_EXECUTABLE valgrind)
if(VALGRIND_EXECUTABLE STREQUAL "VALGRIND_EXECUTABLE-NOTFOUND")
  message(STATUS "Valgrind not found.")
  set(VALGRIND_FOUND FALSE CACHE INTERNAL "")
  unset(VALGRIND_EXECUTABLE)
else()
  message(STATUS "Valgrind found: ${VALGRIND_EXECUTABLE}")
  set(VALGRIND_FOUND TRUE CACHE INTERNAL "")
endif()