# PAM authentication and various other authentication related issues:
# 1. PAM authentication, normal + roles (MXS-2478)
# 2. Multiple authenticators on the same listener (MXS-2497)
# 3. skip_authentication and match_host (MXS-2941)
# 4. lower_case_table_names
# 5. log_password_mismatch (MXS-2916)
# 6. pam_backend_mapping=mariadb (MXS-3475)
#
# Also tests MXS-1662: PAM admin authentication
add_test_executable_ex(NAME pam_authentication SOURCE pam_authentication.cpp
        CONFIG pam_authentication.cnf VMS repl_backend)

# MXS-3225, MXS-2383 Pam two factor authentication (2FA) test. Works only on Centos so far, so label as
# HEAVY to prevent runs on general test runs.
add_test_executable_ex(NAME pam_authentication_2fa SOURCE pam_authentication_2fa.cpp
        CONFIG pam_authentication_2fa.cnf VMS repl_backend LABELS HEAVY)

# User accounts file + xpand + pam. MXS-3751.
add_test_executable_ex(NAME xpand_mapped_auth SOURCE xpand_mapped_auth.cc
        CONFIG xpand_mapped_auth.cnf VMS repl_backend xpand_backend)

# GSSAPI/Kerberos authentication. MXS-3733.
add_test_executable_ex(NAME gss_authentication SOURCE gss_authentication.cc
        CONFIG gss_authentication.cnf VMS repl_backend)

# Executes change_user commands. Also tests MXS-3366.
add_test_executable_ex(NAME mariadb_change_user SOURCE mariadb_change_user.cc CONFIG mariadb_change_user.cnf
        VMS repl_backend LABELS MariaDBAuth LIGHT)