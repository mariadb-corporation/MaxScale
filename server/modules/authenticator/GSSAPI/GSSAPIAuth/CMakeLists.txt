add_library(GSSAPIAuth SHARED gssapi_auth.c ../gssapi_auth_common.c)
target_link_libraries(GSSAPIAuth maxscale-common ${GSSAPI_LIBS} ${SQLITE_LIBRARIES} MySQLCommon)
set_target_properties(GSSAPIAuth PROPERTIES VERSION "1.0.0")
install_module(GSSAPIAuth core)
