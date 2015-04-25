add_library(hintfilter SHARED hintfilter.c hintparser.c)
set_target_properties(hintfilter PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${CMAKE_INSTALL_PREFIX}/lib)
target_link_libraries(hintfilter ssl log_manager utils)
install(TARGETS hintfilter DESTINATION ${MAXSCALE_MODULE_INSTALL})
