add_library(maxpgsql STATIC pg_connector.cc)
target_include_directories(maxpgsql PRIVATE ${PostgreSQL_INCLUDE_DIRS})
target_link_libraries(maxpgsql maxbase ${PostgreSQL_LIBRARIES})
set_target_properties(maxpgsql PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
