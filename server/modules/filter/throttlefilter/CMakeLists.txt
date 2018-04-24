add_library(throttlefilter SHARED throttlefilter.cc throttlesession.cc eventcount.cc stopwatch.cc)
target_link_libraries(throttlefilter maxscale-common mysqlcommon)
set_target_properties(throttlefilter PROPERTIES VERSION "1.0.0")
install_module(throttlefilter core)
