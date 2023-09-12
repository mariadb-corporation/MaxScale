/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-08-18
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

namespace
{
string read_password();
int    run_suid_auth(std::unique_ptr<mxb::AsyncProcess> ext_proc, bool mapping_on);
}

int main(int argc, char* argv[])
{
    mxb::Log log(MXB_LOG_TARGET_STDOUT);

    bool debug = false;
    const char accepted_opts[] = "d";
    int opt;
    while ((opt = getopt(argc, argv, accepted_opts)) != -1)
    {
        if (opt == 'd')
        {
            debug = true;
        }
        else
        {
            MXB_ERROR("Invalid argument %c", optopt);
            return EXIT_FAILURE;
        }
    }

    auto mode = AuthMode::PW;
    cout << "Select mode:\n1-Password\n2-Password + 2FA code\n3-SUID wrapper\n";

    int selection = 0;
    cin >> selection;

    switch (selection)
    {
    case 1:
        break;

    case 2:
        mode = AuthMode::PW_2FA;
        break;

    case 3:
        mode = AuthMode::SUID;
        break;

    default:
        cout << "Invalid selection: '" << selection << "'.\n";
        return EXIT_FAILURE;
    }

    int rval = EXIT_FAILURE;
    string username, service;
    cout << "Username:\n";
    cin.ignore();   // ignores the \n left from previous read.
    std::getline(cin, username);

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

    if (mode == AuthMode::PW || mode == AuthMode::PW_2FA)
    {
        cout << "Password:\n";
        string password = read_password();
        string twofa_pw;
        if (mode == AuthMode::PW_2FA)
        {
            cout << "Two-factor authenticator code:\n";
            twofa_pw = read_password();
        }

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
    }
    else
    {
        string total_path = mxb::pam::gen_auth_tool_run_cmd(debug);
        if (!total_path.empty())
        {
            if (auto ext_cmd = mxb::AsyncCmd::create(total_path, 1000))
            {
                if (auto ext_proc = ext_cmd->start())
                {
                    // Command should have started. The subprocess now expects to read a settings byte,
                    // username and service name.
                    std::vector<uint8_t> first_msg = mxb::pam::create_suid_settings_msg(
                        username, service, mapping_on);

                    if (ext_proc->write(first_msg.data(), first_msg.size()))
                    {
                        rval = run_suid_auth(std::move(ext_proc), mapping_on);
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

int run_suid_auth(std::unique_ptr<mxb::AsyncProcess> ext_proc, bool mapping_on)
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
                                answer = read_password();
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
        if (mapping_on)
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
