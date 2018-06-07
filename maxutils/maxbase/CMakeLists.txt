include_directories(include)
add_library(maxutil SHARED src/eventcount.cc src/stopwatch.cc)
set_target_properties(maxutil PROPERTIES VERSION "1.0.0")
install_module(maxutil core)
