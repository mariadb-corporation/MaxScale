add_library(pambackendauth SHARED pam_backend_auth.cc ../pam_auth_common.cc pam_backend_session.cc)
target_link_libraries(pambackendauth maxscale-common mysqlcommon ${SQLITE_LIBRARIES})
set_target_properties(pambackendauth PROPERTIES VERSION "1.0.0")
install_module(pambackendauth core)
