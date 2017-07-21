add_library(PAMAuth SHARED pam_auth.cc ../pam_auth_common.cc pam_client_session.cc pam_instance.cc)
target_link_libraries(PAMAuth maxscale-common ${PAM_LIBRARIES} ${SQLITE_LIBRARIES} MySQLCommon)
set_target_properties(PAMAuth PROPERTIES VERSION "1.0.0")
install_module(PAMAuth core)
