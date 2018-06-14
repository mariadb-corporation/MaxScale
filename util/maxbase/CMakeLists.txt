add_library(base SHARED eventcount.cc stopwatch.cc)
set_target_properties(base PROPERTIES VERSION "1.0.0")
install_module(base core)
