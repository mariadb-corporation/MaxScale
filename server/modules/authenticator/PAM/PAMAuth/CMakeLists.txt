add_library(pamauth SHARED pam_auth.cc ../pam_auth_common.cc pam_client_session.cc pam_instance.cc)
target_link_libraries(pamauth maxscale-common ${PAM_LIBRARIES} ${SQLITE_LIBRARIES} mysqlcommon)
set_target_properties(pamauth PROPERTIES VERSION "1.0.0")
install_module(pamauth core)
