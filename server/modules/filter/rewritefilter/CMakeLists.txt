add_library(rewritefilter SHARED
  rewritefilter.cc
  rewritesession.cc
  )
target_link_libraries(rewritefilter maxscale-common)
set_target_properties(rewritefilter PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(rewritefilter core)
