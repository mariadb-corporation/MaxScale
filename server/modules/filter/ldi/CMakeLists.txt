include(${CMAKE_SOURCE_DIR}/cmake/BuildBoost.cmake)

FetchContent_Declare(
  libmarias3
  GIT_REPOSITORY "https://github.com/markus456/libmarias3.git"
  GIT_TAG        "master"
  SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/libmarias3
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
  PATCH_COMMAND ""
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
