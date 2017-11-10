add_library(binlogfilter SHARED binlogfilter.cc binlogfiltersession.cc)
set_target_properties(binlogfilter PROPERTIES VERSION "1.0.0")
install_module(binlogfilter core)
