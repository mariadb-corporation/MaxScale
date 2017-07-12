# Find the npm executable
#
# The following variables are set:
# NPM_FOUND - True if npm was found
# NPM_EXECUTABLE - Path to npm

find_program(NPM_EXECUTABLE npm)

if (${NPM_EXECUTABLE} MATCHES "NOTFOUND")
  message(STATUS "Could not find npm")
  set(NPM_FOUND FALSE CACHE INTERNAL "")
  unset(NPM_EXECUTABLE)
else()
  message(STATUS "Found npm: ${NPM_EXECUTABLE}")
  set(NPM_FOUND TRUE CACHE INTERNAL "")
endif()
