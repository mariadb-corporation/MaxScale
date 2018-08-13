add_library(maxbase STATIC eventcount.cc stopwatch.cc stacktrace.cc logger.cc)
set_target_properties(maxbase PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install(TARGETS maxbase DESTINATION lib)
