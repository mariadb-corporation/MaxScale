if (BUILD_QC_MYSQLEMBEDDED)
  find_package(MySQL)
  # Include the embedded library headers
  subdirs(MYSQL_INCLUDE_DIR_ALL ${MYSQL_EMBEDDED_INCLUDE_DIR})
  foreach(DIR ${MYSQL_INCLUDE_DIR_ALL})
    include_directories(BEFORE ${DIR})
  endforeach()
  include_directories(BEFORE ${MYSQL_EMBEDDED_INCLUDE_DIR}/..)

  add_library(qc_mysqlembedded SHARED qc_mysqlembedded.cc)

  include_directories(${MYSQL_INCLUDE_DIR})
  target_link_libraries(qc_mysqlembedded ${MYSQL_EMBEDDED_LIBRARIES} aio crypt crypto dl m ${PCRE_LINK_FLAGS} ssl stdc++ z)
  set_target_properties(qc_mysqlembedded PROPERTIES VERSION "1.0.0")
  set_target_properties(qc_mysqlembedded PROPERTIES LINK_FLAGS -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/qc_mysqlembedded.map)
  #set_target_properties(qc_mysqlembedded PROPERTIES LINK_FLAGS -Wl,-z,defs)
  install_module(qc_mysqlembedded libmysqld-parser)
  install(PROGRAMS ${ERRMSG} DESTINATION ${MAXSCALE_VARDIR}/lib/maxscale)
endif()
