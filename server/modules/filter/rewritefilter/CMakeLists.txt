add_library(rewritefilter SHARED
  rewritefilter.cc
  rewritesession.cc
  sql_rewriter.cc
  template_reader.cc
  native_rewriter.cc
  native_replacer.cc
  regex_rewriter.cc
  json_reader.cc
  )
target_link_libraries(rewritefilter maxscale-common)
set_target_properties(rewritefilter PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(rewritefilter core)
