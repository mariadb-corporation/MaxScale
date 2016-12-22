add_library(masking SHARED
  maskingfilter.cc
  maskingfiltersession.cc
  )

target_link_libraries(masking maxscale-common)
set_target_properties(masking PROPERTIES VERSION "1.0.0")
set_target_properties(masking PROPERTIES LINK_FLAGS -Wl,-z,defs)
install_module(masking experimental)
