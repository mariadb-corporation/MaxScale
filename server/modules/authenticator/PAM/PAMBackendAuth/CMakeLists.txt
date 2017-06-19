add_library(PAMBackendAuth SHARED pam_backend_auth.cc ../pam_auth_common.cc)
target_link_libraries(PAMBackendAuth maxscale-common MySQLCommon ${SQLITE_LIBRARIES})
set_target_properties(PAMBackendAuth PROPERTIES VERSION "1.0.0")
install_module(PAMBackendAuth core)
