add_library(hintfilter SHARED hintfilter.c hintparser.c)
set_target_properties(hintfilter PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${CMAKE_INSTALL_PREFIX}/lib)
target_link_libraries(hintfilter utils ssl log_manager)
install(TARGETS hintfilter DESTINATION modules)