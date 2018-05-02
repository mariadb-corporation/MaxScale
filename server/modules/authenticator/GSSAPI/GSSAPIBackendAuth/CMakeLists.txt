add_library(gssapibackendauth SHARED gssapi_backend_auth.cc ../gssapi_auth_common.cc)
target_link_libraries(gssapibackendauth maxscale-common  ${GSSAPI_LIBS} mysqlcommon)
set_target_properties(gssapibackendauth PROPERTIES VERSION "1.0.0")
install_module(gssapibackendauth core)
