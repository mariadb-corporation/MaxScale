# The libmarias3 library is not available in any of the repositories which means
# that the LDI filter cannot be built if it can't be bundled.
if (NOT BUNDLE)
  message(STATUS "Will not build the ldi filter with BUNDLE=N, need to download libmarias3.")
  return()
endif()

set(LIBMARIAS3_REPO "https://github.com/mariadb-corporation/libmarias3.git" CACHE STRING "libmarias3 git repository")

FetchContent_Declare(
  libmarias3
  GIT_REPOSITORY ${LIBMARIAS3_REPO}
  GIT_TAG "master"
  SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/libmarias3
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
  # We need a config.h in the source directory. This is usually (?) created by the autotools invocation and
  # previously it got included from _somewhere_. The file is not really needed and an empty files seems to
  # work.
  PATCH_COMMAND touch ${CMAKE_CURRENT_BINARY_DIR}/libmarias3/src/config.h
  UPDATE_COMMAND ""
)
FetchContent_MakeAvailable(libmarias3)

include_directories(${CMAKE_CURRENT_BINARY_DIR}/libmarias3/)

set(MS3 ${CMAKE_CURRENT_BINARY_DIR}/libmarias3/)
add_library(ldi SHARED
  ldi.cc
  ldisession.cc
  ldiparser.cc
  ${MS3}/src/assume_role.c
  ${MS3}/src/debug.c
  ${MS3}/src/error.c
  ${MS3}/src/marias3.c
  ${MS3}/src/request.c
  ${MS3}/src/response.c
  ${MS3}/src/sha256-internal.c
  ${MS3}/src/sha256.c
  ${MS3}/src/xml.c
)

target_link_libraries(ldi maxscale-common curl)
set_target_properties(ldi PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(ldi core)

if (BUILD_TESTS)
  add_subdirectory(test)
endif()
