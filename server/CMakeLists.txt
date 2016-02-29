# Use the client libraries in MaxScale core
include_directories(${MARIADB_CONNECTOR_INCLUDE_DIR})

add_subdirectory(core)
add_subdirectory(modules)
add_subdirectory(inih)
