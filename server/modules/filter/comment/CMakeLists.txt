add_library(comment SHARED commentfilter.cc commentfiltersession.cc)
set_target_properties(comment PROPERTIES VERSION "1.0.0")
install_module(comment core)

