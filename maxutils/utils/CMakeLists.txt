if(BUILD_TOOLS)
    # Tool for testing local PAM config.
    add_executable(test_pam_login test_pam_login.cc)
    target_link_libraries(test_pam_login maxbase)
    install_executable(test_pam_login core)
endif()

# For now, only build suid test and tool when testing. Change back once the feature is finished.
if (BUILD_TESTS)
    # Tool for testing local PAM config with suid wrapper.
    add_executable(test_pam_login2 test_pam_login2.cc)
    target_link_libraries(test_pam_login2 maxbase)
    install_executable(test_pam_login2 core)

    # Pam suid wrapper. Requires setuid permission
    add_executable(maxscale_pam_auth_tool pam_auth_tool.cc)
    target_link_libraries(maxscale_pam_auth_tool maxbase)
    install_executable_setuid(maxscale_pam_auth_tool core)
endif()
