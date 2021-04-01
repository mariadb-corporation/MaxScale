/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <termios.h>
#include <maxbase/log.hh>
#include <maxbase/pam_utils.hh>

using std::string;
using std::cin;
using std::cout;
using PamResult = mxb::pam::AuthResult::Result;

int main()
{
    mxb::Log log(MXB_LOG_TARGET_STDOUT);
    string username, password, twofa_pw, service;

    cout << "Username:\n";
    std::getline(cin, username);

    // Disable echo when reading password.
    termios orig_flags {0};
    const auto fd = fileno(stdin);
    tcgetattr(fd, &orig_flags);
    auto new_flags = orig_flags;
    new_flags.c_lflag &= ~ECHO;
    new_flags.c_lflag |= ECHONL;

    bool tc_error = (tcsetattr(fd, TCSANOW, &new_flags) != 0);
    if (!tc_error)
    {
        cout << "Password:\n";
        std::getline(cin, password);
        cout << "Two-factor authenticator code (optional):\n";
        std::getline(cin, twofa_pw);
        // Re-enable echo.
        tc_error = (tcsetattr(fd, TCSANOW, &orig_flags) != 0);
    }

    if (tc_error)
    {
        perror("tcsetattr");
        return EXIT_FAILURE;
    }

    cout << "PAM service:\n";
    std::getline(cin, service);

    cout << "Username mapping enabled (Y/N, optional, default: N):\n";
    string mapping_on_str;
    std::getline(cin, mapping_on_str);
    bool mapping_on = false;
    if (mapping_on_str == "Y" || mapping_on_str == "y")
    {
        mapping_on = true;
    }

    int rval = EXIT_FAILURE;
    auto mode = twofa_pw.empty() ? mxb::pam::AuthMode::PW : mxb::pam::AuthMode::PW_2FA;
    mxb::pam::UserData user = {username, ""};
    mxb::pam::PwdData pwds = {password, twofa_pw};
    mxb::pam::AuthSettings sett = {service, mapping_on};
    mxb::pam::ExpectedMsgs exp = {"Password", ""};

    auto res = mxb::pam::authenticate(mode, user, pwds, sett, exp);
    if (res.type == PamResult::SUCCESS)
    {
        cout << "Authentication successful.";
        if (mapping_on)
        {
            cout << " Username mapped to '" << res.mapped_user << "'.";
        }
        cout << "\n";
        rval = EXIT_SUCCESS;
    }
    else if (res.error.empty())
    {
        string failtype = (res.type == PamResult::WRONG_USER_PW) ? "wrong username/password" :
            (res.type == PamResult::ACCOUNT_INVALID) ? "account error" : "unknown error";
        cout << "Authentication failed: " << failtype << ".\n";
    }
    else
    {
        cout << res.error << "\n";
    }
    return rval;
}
