add_library(qc_pp SHARED qc_pp.c)

target_link_libraries(qc_pp maxscale-common)
set_target_properties(qc_pp PROPERTIES LINK_FLAGS -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/qc_pp.map)
set_target_properties(qc_pp PROPERTIES LINK_FLAGS -Wl,-z,defs)

install(TARGETS qc_pp COMPONENT lib DESTINATION ${MAXSCALE_LIBDIR})
