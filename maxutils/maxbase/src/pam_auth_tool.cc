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

#include <cstring>
#include <getopt.h>
#include <errno.h>
#include <unistd.h>
#include <maxbase/log.hh>
#include <maxbase/pam_utils.hh>

using std::string;
using namespace mxb::pam;

namespace
{
bool read_settings(int fd, bool* mapping_out, string* uname_out, string* pam_service_out);
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

    MXB_DEBUG("PAM sandbox started [%s].", argv[0]);
    // Try to run as root. Even if it fails, proceed.
    if (setreuid(0, 0) != 0)
    {
        MXB_WARNING("PAM sandbox: setreuid() failed. Error %i: %s", errno, strerror(errno));
    }

    // Read some settings from stdio. Passing the values as command line arguments would be more convenient
    // but doing so would show username and pam service in "ps aux" and similar process lists.
    const int in_fd = STDIN_FILENO;
    const int out_fd = STDOUT_FILENO;
    bool mapping_on = false;
    string uname;
    string pam_service;

    int rc = -1;
    if (read_settings(in_fd, &mapping_on, &uname, &pam_service))
    {
        UserData user_data = {uname, ""};
        AuthSettings sett = {pam_service, mapping_on};
        auto res = authenticate_fd(in_fd, out_fd, user_data, sett);
        if (res.type == AuthResult::Result::SUCCESS)
        {
            MXB_DEBUG("PAM sandbox: authentication succeeded, sending EOF.");
            uint8_t ok = SBOX_EOF;
            if (write(out_fd, &ok, sizeof(ok)) == sizeof(ok))
            {
                rc = 0;
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
bool read_settings(int fd, bool* mapping_out, string* uname_out, string* pam_service_out)
{
    bool success = false;
    uint8_t bits;
    if (read(fd, &bits, sizeof(bits)) == sizeof(bits))
    {
        bool mapping = bits & SBOX_CFG_MAP;
        if (mapping)
        {
            MXB_DEBUG("PAM sandbox: mapping is on.");
        }

        auto uname = mxb::pam::read_string_blocking(fd);
        if (uname)
        {
            MXB_DEBUG("PAM sandbox: username is '%s'.", uname->c_str());
            auto pam_service = mxb::pam::read_string_blocking(fd);
            if (pam_service)
            {
                MXB_DEBUG("PAM sandbox: pam service is '%s'.", pam_service->c_str());
                *mapping_out = mapping_out;
                *uname_out = std::move(*uname);
                *pam_service_out = std::move(*pam_service);
                success = true;
            }
        }
    }
    return success;
}
}
