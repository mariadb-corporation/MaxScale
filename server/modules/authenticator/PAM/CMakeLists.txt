add_library(pamauth SHARED pam_auth_common.cc pam_client_session.cc pam_backend_session.cc pam_instance.cc)
target_link_libraries(pamauth maxscale-common mariadbprotocol)
set_target_properties(pamauth PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(pamauth core)
