include_directories(include)
add_library(maxbase SHARED src/eventcount.cc src/stopwatch.cc)
set_target_properties(maxbase PROPERTIES VERSION "1.0.0")
install_module(maxbase core)
