add_library(auth_utils SHARED auth_utils.cc)
target_link_libraries(auth_utils maxtest)
set_target_properties(auth_utils PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install(TARGETS auth_utils DESTINATION system-test/authentication)
add_dependencies(auth_utils maxtest)

add_test_executable_ex(NAME pam_authentication_suid SOURCE pam_authentication_suid.cc
        CONFIG pam_authentication_suid.cnf VMS repl_backend LIBS auth_utils)

# PAM authentication and various other authentication related issues:
# 1. PAM authentication, normal + roles (MXS-2478)
# 2. Multiple authenticators on the same listener (MXS-2497)
# 3. skip_authentication and match_host (MXS-2941)
# 4. lower_case_table_names
# 5. log_password_mismatch (MXS-2916)
# 6. pam_backend_mapping=mariadb (MXS-3475)
# 7. Token authentication with PAM users (MXS-4355)
# 8. com_change_user between different authenticators (MXS-4731)
#
# Also tests MXS-1662: PAM admin authentication
add_test_executable_ex(NAME pam_authentication SOURCE pam_authentication.cc
        CONFIG pam_authentication.cnf VMS repl_backend)

# MXS-3225, MXS-2383 Pam two factor authentication (2FA) test. Works only on Centos so far, so label as
# HEAVY to prevent runs on general test runs.
add_test_executable_ex(NAME pam_authentication_2fa SOURCE pam_authentication_2fa.cc
        CONFIG pam_authentication_2fa.cnf VMS repl_backend LABELS HEAVY)

# GSSAPI/Kerberos authentication. MXS-3733.
add_test_executable_ex(NAME gss_authentication SOURCE gss_authentication.cc
        CONFIG gss_authentication.cnf VMS repl_backend)

# Executes change_user commands. Also tests MXS-3366.
add_test_executable_ex(NAME mariadb_change_user SOURCE mariadb_change_user.cc CONFIG mariadb_change_user.cnf
        VMS repl_backend LABELS MariaDBAuth LIGHT)

# MXS-2355: Try to log in using a different authentication plugin
# MXS-4094: Wrong auth plugin and no pw
add_test_executable_ex(NAME wrong_client_auth_plugin SOURCE wrong_client_auth_plugin.cc
        CONFIG wrong_client_auth_plugin.cnf VMS repl_backend)

# MXS-4270: Ed25519 authentication test
add_test_executable_ex(NAME ed25519_authentication SOURCE ed25519_authentication.cc
        CONFIG ed25519_authentication.cnf VMS repl_backend)

# MXS-4506: passthrough authentication test
# MXS-4723: passthrough + change user
add_test_executable_ex(NAME auth_mariadb_passthrough SOURCE auth_mariadb_passthrough.cc
        CONFIG auth_mariadb_passthrough.cnf VMS repl_backend LIBS auth_utils)

# Regression case for the bug "MaxScale ignores host in user authentication"
add_test_executable_ex(NAME auth_hostname SOURCE auth_hostname.cc CONFIG auth_hostname.cnf
        VMS repl_backend LABELS MariaDBAuth)
