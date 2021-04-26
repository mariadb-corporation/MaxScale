# PAM authentication and various other authentication related issues:
# 1. PAM authentication, normal + roles (MXS-2478)
# 2. Multiple authenticators on the same listener (MXS-2497)
# 3. skip_authentication and match_host (MXS-2941)
# 4. lower_case_table_names
# 5. log_password_mismatch (MXS-2916)
#
# Also tests MXS-1662: PAM admin authentication
add_test_executable_ex(NAME pam_authentication SOURCE pam_authentication.cpp
        CONFIG pam_authentication.cnf VMS repl_backend)

# MXS-3225, MXS-2383 Pam two factor authentication (2FA) test. Works only on Centos so far, so label as
# HEAVY to prevent runs on general test runs.
add_test_executable_ex(NAME pam_authentication_2fa SOURCE pam_authentication_2fa.cpp
        CONFIG pam_authentication_2fa.cnf VMS repl_backend LABELS HEAVY)
