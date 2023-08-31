if(BUILD_TOOLS)
    # Tool for testing local PAM config.
    add_executable(test_pam_login test_pam_login.cc)
    target_link_libraries(test_pam_login maxbase)
    install_executable(test_pam_login core)
endif()

# Pam suid wrapper. Requires setuid permission
add_executable(maxscale_pam_auth_tool pam_auth_tool.cc)
target_link_libraries(maxscale_pam_auth_tool maxbase)
install_executable_setuid(maxscale_pam_auth_tool core)
