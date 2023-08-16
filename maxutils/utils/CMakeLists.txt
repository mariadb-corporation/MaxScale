if(BUILD_TOOLS)
    # Tool for testing local PAM config.
    add_executable(test_pam_login test_pam_login.cc)
    target_link_libraries(test_pam_login maxbase)
    install_executable(test_pam_login core)
endif()
