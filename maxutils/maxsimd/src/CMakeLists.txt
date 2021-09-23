add_library(maxsimd STATIC
  generic_canonical.cc
  canonical.cc
  generic_multistmt.cc
  multistmt.cc
  )

target_link_libraries(maxsimd maxbase simdimpl)
add_dependencies(maxsimd maxbase simdimpl)
set_target_properties(maxsimd PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
