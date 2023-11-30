/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <getopt.h>
#include <iostream>
#include <sys/poll.h>
#include <termios.h>
#include <maxbase/externcmd.hh>
#include <maxbase/log.hh>
#include <maxbase/pam_utils.hh>
#include <maxbase/string.hh>

using std::string;
using std::cin;
using std::cout;
using PamResult = mxb::pam::AuthResult::Result;
using mxb::pam::AuthMode;
using std::optional;

namespace
{
string read_password();
int run_suid_auth(std::unique_ptr<mxb::AsyncProcess> ext_proc, const optional<string>& pw,
                  const optional<string>& pw2);

const char usage[] = R"(Usage: test_pam_login [OPTION]
  -d, --debug              debug printing enabled, only affects SUID mode
  -m, --mode=NUM           pam mode: 1-Password, 2-Password+2FA, 3-SUID subprocesss
  -u, --user=USER          username
  -s, --service=SERVICE    pam service
  -p, --password=PASSWORD  password (can be empty)
  -f, --password2=PASSWORD 2nd password (2FA code)
)";
}

int main(int argc, char* argv[])
{
    mxb::Log log(MXB_LOG_TARGET_STDOUT);

    bool debug = false;
    optional<AuthMode> auth_mode;
    string username, service;
    optional<string> password, twofa_pw;

    const char short_opts[] = "dm:u:s:p::f::h";
    option long_opts[] = {
        {"debug",     no_argument,       0, 'd'},
        {"mode",      required_argument, 0, 'm'},
        {"user",      required_argument, 0, 'u'},
        {"service",   required_argument, 0, 's'},
        {"password",  optional_argument, 0, 'p'},
        {"password2", optional_argument, 0, 'f'},
        {"help",      no_argument,       0, 'h'},
        {0,           0,                 0, 0  }
    };

    const char mode_str[] = "1-Password\n2-Password + 2FA code\n3-SUID wrapper\n";
    int opt = -1;

    do
    {
        opt = getopt_long(argc, argv, short_opts, long_opts, nullptr);
        if (opt == 'd')
        {
            debug = true;
        }
        else if (opt == 'm')
        {
            if (*optarg == '1')
            {
                auth_mode = AuthMode::PW;
            }
            else if (*optarg == '2')
            {
                auth_mode = AuthMode::PW_2FA;
            }
            else if (*optarg == '3')
            {
                auth_mode = AuthMode::SUID;
            }
            else
            {
                cout << "Invalid option argument. Valid mode selections are:\n";
                cout << mode_str;
                return EXIT_FAILURE;
            }
        }
        else if (opt == 'u')
        {
            username = optarg;
        }
        else if (opt == 's')
        {
            service = optarg;
        }
        else if (opt == 'p')
        {
            password = optarg ? optarg : "";
        }
        else if (opt == 'f')
        {
            twofa_pw = optarg ? optarg : "";
        }
        else if (opt == 'h')
        {
            cout << usage;
            return EXIT_SUCCESS;
        }
        else if (opt != -1)
        {
            cout << "Invalid argument " << (char)optopt << "\n";
            cout << usage;
            return EXIT_FAILURE;
        }
    }
    while (opt != -1);

    if (!auth_mode.has_value())
    {
        cout << "Select mode:\n";
        cout << mode_str;

        int selection = 0;
        cin >> selection;
        cin.ignore();   // ignores the \n left over from previous read.

        switch (selection)
        {
        case 1:
            auth_mode = AuthMode::PW;
            break;

        case 2:
            auth_mode = AuthMode::PW_2FA;
            break;

        case 3:
            auth_mode = AuthMode::SUID;
            break;

        default:
            cout << "Invalid selection: '" << selection << "'.\n";
            return EXIT_FAILURE;
        }
    }

    int rval = EXIT_FAILURE;
    if (username.empty())
    {
        cout << "Username:\n";
        std::getline(cin, username);
    }

    if (service.empty())
    {
        cout << "PAM service:\n";
        std::getline(cin, service);
    }

    if (*auth_mode == AuthMode::PW || *auth_mode == AuthMode::PW_2FA)
    {
        if (!password.has_value())
        {
            cout << "Password:\n";
            password = read_password();
        }

        if (*auth_mode == AuthMode::PW_2FA && !twofa_pw.has_value())
        {
            cout << "Two-factor authenticator code:\n";
            twofa_pw = read_password();
        }
        else
        {
            twofa_pw = "";
        }

        mxb::pam::UserData user = {username, ""};
        mxb::pam::PwdData pwds = {*password, *twofa_pw};
        mxb::pam::ExpectedMsgs exp = {"Password", ""};

        auto res = mxb::pam::authenticate(*auth_mode, user, pwds, service, exp);
        if (res.type == PamResult::SUCCESS)
        {
            cout << "Authentication successful.";
            if (!res.mapped_user.empty())
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
    }
    else
    {
        string total_path = mxb::pam::gen_auth_tool_run_cmd(debug ? mxb::pam::Debug::YES :
                                                            mxb::pam::Debug::NO);
        if (!total_path.empty())
        {
            if (auto ext_cmd = mxb::AsyncCmd::create(total_path, 1000))
            {
                if (auto ext_proc = ext_cmd->start())
                {
                    // Command should have started. The subprocess now expects to read username and service.
                    std::vector<uint8_t> first_msg = mxb::pam::create_suid_settings_msg(username, service);
                    if (ext_proc->write(first_msg.data(), first_msg.size()))
                    {
                        rval = run_suid_auth(std::move(ext_proc), password, twofa_pw);
                    }
                }
            }
        }
    }
    return rval;
}

namespace
{
string read_password()
{
    // Disable echo when reading password.
    termios orig_flags {0};
    const auto fd = fileno(stdin);
    tcgetattr(fd, &orig_flags);
    auto new_flags = orig_flags;
    new_flags.c_lflag &= ~ECHO;
    new_flags.c_lflag |= ECHONL;

    string rval;
    bool tc_error = (tcsetattr(fd, TCSANOW, &new_flags) != 0);
    if (!tc_error)
    {
        std::getline(cin, rval);
        // Re-enable echo.
        tc_error = (tcsetattr(fd, TCSANOW, &orig_flags) != 0);
    }

    if (tc_error)
    {
        perror("tcsetattr");
    }
    return rval;
}

int run_suid_auth(std::unique_ptr<mxb::AsyncProcess> ext_proc, const optional<string>& pw,
                  const optional<string>& pw2)
{
    const char invalid_msg[] = "Invalid message from subprocess.\n";
    bool auth_success = false;
    string mapped_user;

    string msgs_buf;
    bool keep_running = true;
    int conv_msg_num = 0;
    // Continue reading and writing until EOF.
    while (keep_running)
    {
        auto data = ext_proc->read_output();
        keep_running = data.has_value();
        if (keep_running)
        {
            msgs_buf.append(*data);
            while (!msgs_buf.empty() && keep_running)
            {
                auto [type, message] = mxb::pam::next_message(msgs_buf);
                if (type == mxb::pam::SBOX_CONV)
                {
                    bool conv_ok = false;
                    if (conv_msg_num >= 2)
                    {
                        // Have already sent two questions to client, more is not supported (for now).
                        cout << "Pam asked more than two questions. Not supported.\n";
                    }
                    else if (message.empty())
                    {
                        // The CONV-message should have at least style byte.
                        cout << invalid_msg;
                    }
                    else
                    {
                        uint8_t conv_type = message[0];
                        if (conv_type == 2 || conv_type == 4)
                        {
                            // Message without contents is allowed.
                            if (message.length() > 1)
                            {
                                std::string_view msg(&message[1], message.length() - 1);
                                cout << msg;
                            }
                            else
                            {
                                cout << "<empty message, expecting input>\n";
                            }

                            string answer;
                            if (conv_type == 2)
                            {
                                std::getline(cin, answer);
                            }
                            else
                            {
                                // Echo off.
                                auto& cmdline_answer = (conv_msg_num == 0) ? pw : pw2;
                                if (cmdline_answer.has_value())
                                {
                                    answer = *cmdline_answer;
                                }
                                else
                                {
                                    answer = read_password();
                                }
                            }

                            std::vector<uint8_t> answer_msg;
                            mxb::pam::add_string(answer, &answer_msg);
                            if (ext_proc->write(answer_msg.data(), answer_msg.size()))
                            {
                                conv_ok = true;
                            }
                        }
                        else
                        {
                            cout << invalid_msg;
                        }
                        conv_msg_num++;
                    }

                    keep_running = conv_ok;
                }
                else if (type == mxb::pam::SBOX_AUTHENTICATED_AS)
                {
                    mapped_user = std::move(message);
                }
                else if (type == mxb::pam::SBOX_EOF)
                {
                    auth_success = true;
                    keep_running = false;
                }
                else if (type == mxb::pam::SBOX_WARN)
                {
                    cout << "Warning: " << message << "\n";
                }
                else if (type == 0)
                {
                    // Incomplete message, wait for more data from external process.
                    break;
                }
                else
                {
                    cout << invalid_msg;
                    keep_running = false;
                }
            }

            if (keep_running)
            {
                // Check that child is still running and poll for more data.
                if (ext_proc->try_wait() == mxb::Process::TIMEOUT)
                {
                    pollfd pfd;
                    pfd.fd = ext_proc->read_fd();
                    pfd.events = POLLIN;
                    if (poll(&pfd, 1, 10 * 1000) == -1)
                    {
                        cout << "Failed to poll pipe file descriptor. Error " << errno << ": " <<
                            mxb_strerror(errno);
                        keep_running = false;
                    }
                }
                else
                {
                    keep_running = false;
                }
            }
        }
    }

    int rval = EXIT_FAILURE;
    int sbox_rc = ext_proc->wait();
    if (auth_success)
    {
        cout << "Authentication successful.";
        if (!mapped_user.empty())
        {
            cout << " Username mapped to '" << mapped_user << "'.";
        }
        cout << "\n";
        rval = EXIT_SUCCESS;

        if (sbox_rc == 0)
        {
            rval = EXIT_SUCCESS;
        }
        else
        {
            cout << "SUID sandbox returned fail status " << sbox_rc << ".\n";
        }
    }
    else
    {
        cout << "Authentication failed.";
    }
    return rval;
}
}
