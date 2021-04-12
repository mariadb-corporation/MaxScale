  add_library(mariadbauth OBJECT mysql_auth.cc)
  set_target_properties(mariadbauth PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
  target_link_libraries(mariadbauth maxbase)
