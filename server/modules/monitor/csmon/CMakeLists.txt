find_package(LibXml2 REQUIRED)

include_directories(${LIBXML2_INCLUDE_DIR})

add_library(csmon SHARED
  columnstore.cc
  csconfig.cc
  cscontext.cc
  csmon.cc
  csmonitor.cc
  csmonitorserver.cc)
target_link_libraries(csmon maxscale-common ${LIBXML2_LIBRARIES})
set_target_properties(csmon PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(csmon core)

if (BUILD_TOOLS)
  add_executable(csxml csxml.cc)
  target_link_libraries(csxml csmon maxscale-common ${LIBXML2_LIBRARIES})
endif()

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
