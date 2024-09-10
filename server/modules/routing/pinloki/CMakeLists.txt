find_package(Boost REQUIRED)

if (Boost_VERSION VERSION_LESS_EQUAL "1.66")
  # Compilation with GCC on Rocky Linux 8 fails with an internal compiler error
  # if LTO is enabled. Disabling LTO for older boost versions avoids it.
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO FALSE)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE FALSE)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL FALSE)
endif()

add_library(pinloki-core OBJECT
  file_transformer.cc
  binlog_file.cc
  shared_binlogs.cc
  ifstream_reader.cc
  config.cc
  dbconnection.cc
  file_reader.cc
  file_writer.cc
  find_gtid.cc
  gtid.cc
  inventory.cc
  maria_rpl_event.cc
  reader.cc
  rpl_event.cc
  writer.cc
  parser.cc
  )

target_link_libraries(pinloki-core maxscale-common uuid)

# Clang 15 complains about deprecated builtins in the boost headers without this
target_compile_options(pinloki-core PRIVATE "-Wno-deprecated-builtins")

add_library(binlogrouter SHARED pinloki.cc pinlokisession.cc)
target_link_libraries(binlogrouter pinloki-core maxscale-common ${ZSTD_LIBRARIES} stdc++fs)
set_target_properties(binlogrouter PROPERTIES VERSION "1.0.0"  LINK_FLAGS -Wl,-z,defs)
install_module(binlogrouter core)

if (BUILD_TESTS)
  add_subdirectory(test)
endif()
