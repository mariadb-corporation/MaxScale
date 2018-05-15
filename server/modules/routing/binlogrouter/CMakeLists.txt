add_library(binlogrouter SHARED blr.c blr_master.c blr_cache.c blr_slave.c blr_file.c)
set_target_properties(binlogrouter PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${MAXSCALE_LIBDIR} VERSION "2.0.0")
set_target_properties(binlogrouter PROPERTIES LINK_FLAGS -Wl,-z,defs)
target_link_libraries(binlogrouter maxscale-common ${PCRE_LINK_FLAGS} uuid)
install_module(binlogrouter core)

add_executable(maxbinlogcheck maxbinlogcheck.c blr_file.c blr_cache.c blr_master.c blr_slave.c blr.c)
target_link_libraries(maxbinlogcheck maxscale-common ${PCRE_LINK_FLAGS} uuid)

install_executable(maxbinlogcheck core)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-format-overflow -Wno-format-truncation")

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
