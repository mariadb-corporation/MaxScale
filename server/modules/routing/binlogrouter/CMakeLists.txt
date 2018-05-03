add_library(binlogrouter SHARED blr.cc blr_master.cc blr_cache.cc blr_slave.cc blr_file.cc)
set_target_properties(binlogrouter PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${MAXSCALE_LIBDIR} VERSION "2.0.0")
set_target_properties(binlogrouter PROPERTIES LINK_FLAGS -Wl,-z,defs)
target_link_libraries(binlogrouter maxscale-common ${PCRE_LINK_FLAGS} uuid)
install_module(binlogrouter core)

add_executable(maxbinlogcheck maxbinlogcheck.cc blr_file.cc blr_cache.cc blr_master.cc blr_slave.cc blr.cc)
target_link_libraries(maxbinlogcheck maxscale-common ${PCRE_LINK_FLAGS} uuid)

install_executable(maxbinlogcheck core)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
