include_directories(${LIBXML2_INCLUDE_DIR})

add_library(simdimpl STATIC
  simd256.cc
  simd_canonical.cc
  simd_multistmt.cc
  )

add_dependencies(simdimpl maxbase)

set_target_properties(simdimpl PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
