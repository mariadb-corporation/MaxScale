  add_library(ed25519auth SHARED ed25519_auth.cc)
  set_target_properties(ed25519auth PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
  target_link_libraries(ed25519auth maxbase maxscale-common)
  install_module(ed25519auth core)
