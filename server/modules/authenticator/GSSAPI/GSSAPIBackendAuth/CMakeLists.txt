add_library(GSSAPIBackendAuth SHARED gssapi_backend_auth.c ../gssapi_auth_common.c)
target_link_libraries(GSSAPIBackendAuth maxscale-common  ${GSSAPI_LIBS} MySQLCommon)
set_target_properties(GSSAPIBackendAuth PROPERTIES VERSION "1.0.0")
install_module(GSSAPIBackendAuth core)
