add_library(qc_mysqlembedded SHARED qc_mysqlembedded.cc)

target_link_libraries(qc_mysqlembedded ${EMBEDDED_LIB} aio crypt crypto dl m ${PCRE_LINK_FLAGS} ssl stdc++ z)
set_target_properties(qc_mysqlembedded PROPERTIES VERSION "1.0.0")
set_target_properties(qc_mysqlembedded PROPERTIES LINK_FLAGS -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/qc_mysqlembedded.map)
#set_target_properties(qc_mysqlembedded PROPERTIES LINK_FLAGS -Wl,-z,defs)
install(TARGETS qc_mysqlembedded COMPONENT lib DESTINATION ${MAXSCALE_LIBDIR})
