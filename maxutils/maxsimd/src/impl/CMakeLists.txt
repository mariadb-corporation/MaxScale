include_directories(${LIBXML2_INCLUDE_DIR})

add_library(simdimpl STATIC
  simd256.cc
  simd_canonical.cc
  )

add_dependencies(simdimpl maxbase)

target_compile_options(simdimpl PRIVATE "-mavx2")
set_target_properties(simdimpl PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
