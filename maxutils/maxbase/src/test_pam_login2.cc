/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <climits>
#include <iostream>
#include <thread>
#include <libgen.h>
#include <termios.h>
#include <sys/poll.h>
#include <maxbase/assert.hh>
#include <maxbase/externcmd.hh>
#include <maxbase/log.hh>
#include <maxbase/pam_utils.hh>
#include <maxbase/string.hh>

using std::string;
using std::cin;
using std::cout;

namespace
{
std::tuple<bool, bool> process_sbox_message(std::string& data, mxb::AsyncProcess& proc);
string                 read_password();
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

    string username;
    string service;

    cout << "Username:\n";
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

    // Get path to current executable. Should typically fit in PATH_MAX.
    string total_path;
    const int max_len = PATH_MAX + 1;
    char buf[max_len];
    const char func_call[] = "readlink(\"/proc/self/exe\")";
    if (auto len = readlink("/proc/self/exe", buf, max_len); len > 0)
    {
        if (len < max_len)
        {
            buf[len] = '\0';
            char* directory = dirname(buf);
            total_path = directory;
            total_path.append("/maxscale_pam_auth_tool");
            if (debug)
            {
                total_path.append(" -d");
            }
        }
        else
        {
            MXB_ERROR("%s returned too much data.", func_call);
        }
    }
    else if (len == 0)
    {
        MXB_ERROR("%s did not return any data.", func_call);
    }
    else
    {
        MXB_ERROR("%s failed. Error %i: '%s'", func_call, errno, mxb_strerror(errno));
    }

    int timeout_s = 10;
    int rval = EXIT_FAILURE;
    if (!total_path.empty())
    {
        if (auto ext_cmd = mxb::AsyncCmd::create(total_path, timeout_s); ext_cmd)
        {
            if (auto ext_proc = ext_cmd->start(); ext_proc)
            {
                bool auth_ok = false;
                // Command should have started. The subprocess now expects to read a settings byte,
                // username and service name.
                std::vector<uint8_t> first_msg;
                uint8_t settings = mapping_on ? 1 : 0;
                first_msg.reserve(100);
                first_msg.push_back(settings);
                mxb::pam::add_string(username, &first_msg);
                mxb::pam::add_string(service, &first_msg);

                if (ext_proc->write(first_msg.data(), first_msg.size()))
                {
                    bool keep_running = true;
                    // Continue reading and writing until EOF.
                    while (keep_running)
                    {
                        auto [read_again, data] = ext_proc->read_output();
                        keep_running = read_again;

                        if (data.empty())
                        {
                            if (keep_running)
                            {
                                // Check that child is still running and poll for more data.
                                if (ext_proc->try_wait() == mxb::Process::TIMEOUT)
                                {
                                    pollfd pfd;
                                    pfd.fd = ext_proc->read_fd();
                                    pfd.events = POLLIN;
                                    if (poll(&pfd, 1, timeout_s * 1000) == -1)
                                    {
                                        MXB_ERROR("Failed to poll pipe file descriptor: %d, %s",
                                                  errno, mxb_strerror(errno));
                                        keep_running = false;
                                    }
                                }
                                else
                                {
                                    keep_running = false;
                                }
                            }
                        }
                        else
                        {
                            // Multiple messages can be read at once if the subprocess sends them in series.
                            // It's a bit unclear if partial messages are possible. It may happen with long
                            // (> 4kB) messages, since the subprocess writes blocking but main process reads
                            // non-blocking.
                            while (!data.empty() && keep_running)
                            {
                                bool io_ok = false;
                                std::tie(io_ok, auth_ok) = process_sbox_message(data, *ext_proc);
                                if (!io_ok || auth_ok)
                                {
                                    mxb_assert(!auth_ok || data.empty());
                                    keep_running = false;
                                }
                            }
                        }
                    }
                }

                int sbox_rc = ext_proc->wait();
                if (auth_ok)
                {
                    MXB_NOTICE("Authentication succeeded.");
                    if (sbox_rc == 0)
                    {
                        rval = EXIT_SUCCESS;
                    }
                    else
                    {
                        MXB_ERROR("Sandbox returned fail status %i.", sbox_rc);
                    }
                }
                else
                {
                    MXB_ERROR("Authentication failed.");
                }
            }
        }
    }
    return rval;
}

namespace
{
std::tuple<bool, bool> process_sbox_message(std::string& data, mxb::AsyncProcess& proc)
{
    bool io_ok = true;
    bool auth_success = false;
    int processed_bytes = 0;

    uint8_t msg_type = data[0];
    switch (msg_type)
    {
    case mxb::pam::SBOX_CONV:
        {
            auto [bytes, message] = mxb::pam::extract_string(&data[1], data.data() + data.size());
            if (!message.empty())
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
                    if (proc.write(answer_msg.data(), answer_msg.size()))
                    {
                        processed_bytes = 1 + bytes;
                    }
                    else
                    {
                        io_ok = false;
                    }
                }
                else
                {
                    io_ok = false;
                }
            }
            else if (bytes < 0)
            {
                io_ok = false;
            }
            // else: incomplete message
        }
        break;

    case mxb::pam::SBOX_EOF:
        auth_success = true;
        processed_bytes = 1;
        break;

    default:
        io_ok = false;
        break;
    }

    if (io_ok)
    {
        data.erase(0, processed_bytes);
    }
    return {io_ok, auth_success};
}

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
}
