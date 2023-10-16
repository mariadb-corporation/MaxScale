# Use RapidJSON for JSON generation, seems to be a lot faster than jansson
set(RAPIDJSON_REPO "https://github.com/Tencent/rapidjson.git" CACHE STRING "RapidJSON git repository")
set(RAPIDJSON_TAG "v1.1.0" CACHE STRING "RapidJSON git tag")

# The GIT_SUBMODULES points to an existing directory in the project which prevents the
# submodules from being cloned. This is required to avoid cloning the gtest submodule
# that RapidJSON uses. See: https://gitlab.kitware.com/cmake/cmake/-/issues/20579
FetchContent_Populate(
  rapidjson
  QUIET
  GIT_REPOSITORY ${RAPIDJSON_REPO}
  GIT_TAG ${RAPIDJSON_TAG}
  GIT_SHALLOW 1
  GIT_SUBMODULES "doc"
  SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/rapidjson)

include_directories(${CMAKE_CURRENT_BINARY_DIR}/rapidjson/include/)

add_library(kafkacdc SHARED kafkacdc.cc)
add_dependencies(kafkacdc replicator librdkafka)
set_target_properties(kafkacdc PROPERTIES VERSION "1.0.0"  LINK_FLAGS -Wl,-z,defs)
target_link_libraries(kafkacdc replicator maxscale-common ${RDKAFKA_LIBRARIES})
install_module(kafkacdc core)
