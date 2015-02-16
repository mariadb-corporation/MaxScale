# This CMake file tries to find the Pandoc executable
# The following variables are set:
# PANDOC_FOUND - System has Pandoc
# PANDOC_EXECUTABLE - The Pandoc executable file
find_program(PANDOC_EXECUTABLE pandoc)
if(PANDOC_EXECUTABLE STREQUAL "PANDOC_EXECUTABLE-NOTFOUND")
  message(STATUS "Pandoc not found.")
  set(PANDOC_FOUND FALSE CACHE INTERNAL "")
  unset(PANDOC_EXECUTABLE)
else()
  message(STATUS "Pandoc found: ${PANDOC_EXECUTABLE}")
  set(PANDOC_FOUND TRUE CACHE INTERNAL "")
endif()
