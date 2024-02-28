/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <cstring>
#include <getopt.h>
#include <errno.h>
#include <unistd.h>
#include <maxbase/format.hh>
#include <maxbase/log.hh>
#include <maxbase/pam_utils.hh>

using std::string;
using namespace mxb::pam;

namespace
{
bool                   read_settings(int fd, string* uname_out, string* pam_service_out);
std::tuple<bool, bool> call_setreuid(uid_t ruid, uid_t euid, int out_fd);
}

int main(int argc, char* argv[])
{
    // Stdout and in are reserved for communicating with main process. Log file is also used by main process
    // so log everything to stderr.
    mxb::Log log(MXB_LOG_TARGET_STDERR);

    const char accepted_opts[] = "d";
    int opt;
    while ((opt = getopt(argc, argv, accepted_opts)) != -1)
    {
        if (opt == 'd')
        {
            mxb_log_set_priority_enabled(LOG_DEBUG, true);
        }
        else
        {
            MXB_ERROR("PAM sandbox: invalid argument %c", optopt);
            return EXIT_FAILURE;
        }
    }

    // Save current real/effective uid.
    uid_t ruid = -1;
    uid_t euid = -1;
    uid_t suid = -1;
    if (getresuid(&ruid, &euid, &suid) != 0)
    {
        // Should not happen.
        MXB_ERROR("getresuid() failed. Error %i: %s", errno, strerror(errno));
        return EXIT_FAILURE;
    }

    MXB_DEBUG("PAM sandbox started [%s].", argv[0]);
    const int in_fd = STDIN_FILENO;
    const int out_fd = STDOUT_FILENO;

    // Read some settings from stdio. Passing the values as command line arguments would be more convenient
    // but doing so would show username and pam service in "ps aux" and similar process lists.
    string uname;
    string pam_service;

    int rc = -1;
    if (read_settings(in_fd, &uname, &pam_service))
    {
        // Try to run as root. Even if it fails, proceed.
        auto [uid_changed, io_ok] = call_setreuid(0, 0, out_fd);
        if (!io_ok)
        {
            return EXIT_FAILURE;
        }

        UserData user_data = {uname, ""};
        auto res = authenticate_fd(in_fd, out_fd, user_data, pam_service);

        if (uid_changed)
        {
            // Change back to original user.
            std::tie(uid_changed, io_ok) = call_setreuid(ruid, euid, out_fd);
            if (!io_ok)
            {
                return EXIT_FAILURE;
            }
        }

        if (res.type == AuthResult::Result::SUCCESS)
        {
            bool send_eof = true;
            if (!res.mapped_user.empty())
            {
                MXB_DEBUG("PAM sandbox: sending authenticated_as field.");
                std::vector<uint8_t> auth_as_msg;
                auth_as_msg.reserve(100);
                auth_as_msg.push_back(SBOX_AUTHENTICATED_AS);
                mxb::pam::add_string(res.mapped_user, &auth_as_msg);
                if (write(out_fd, auth_as_msg.data(), auth_as_msg.size()) != (ssize_t)auth_as_msg.size())
                {
                    send_eof = false;
                }
            }

            if (send_eof)
            {
                MXB_DEBUG("PAM sandbox: authentication succeeded, sending EOF.");
                uint8_t ok = SBOX_EOF;
                if (write(out_fd, &ok, sizeof(ok)) == sizeof(ok))
                {
                    rc = 0;
                }
            }
        }
        else
        {
            MXB_DEBUG("PAM sandbox: authentication failed.");
        }
    }

    MXB_DEBUG("PAM sandbox: closing with rc %i.", rc);
    return rc;
}

namespace
{
bool read_settings(int fd, string* uname_out, string* pam_service_out)
{
    bool success = false;
    auto uname = mxb::pam::read_string_blocking(fd);
    if (uname)
    {
        MXB_DEBUG("PAM sandbox: username is '%s'.", uname->c_str());
        auto pam_service = mxb::pam::read_string_blocking(fd);
        if (pam_service)
        {
            MXB_DEBUG("PAM sandbox: pam service is '%s'.", pam_service->c_str());
            *uname_out = std::move(*uname);
            *pam_service_out = std::move(*pam_service);
            success = true;
        }
    }
    return success;
}

std::tuple<bool, bool> call_setreuid(uid_t ruid, uid_t euid, int out_fd)
{
    bool uid_changed = false;
    bool io_ok = false;
    if (setreuid(ruid, euid) == 0)
    {
        uid_changed = true;
        io_ok = true;
    }
    else
    {
        string msg = mxb::string_printf("setreuid() failed. Error %i: %s", errno, strerror(errno));
        std::vector<uint8_t> warn_msg;
        warn_msg.push_back(SBOX_WARN);
        mxb::pam::add_string(msg, &warn_msg);
        if (write(out_fd, warn_msg.data(), warn_msg.size()) == (ssize_t)warn_msg.size())
        {
            io_ok = true;
        }
    }
    return {uid_changed, io_ok};
}
}
