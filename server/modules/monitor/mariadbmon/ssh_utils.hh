/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-04-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once
#include <maxscale/ccdefs.hh>
#include <memory>

namespace ssh
{
class Session;
class Channel;
}

namespace ssh_util
{
// LibSSH defines the Session-class, which may be shared between multiple channels (i.e. running commands).
// Store it in a shared_ptr to ensure proper destruction.
using SSession = std::shared_ptr<ssh::Session>;

/**
 * Start an SSH session. Reads private key from file, connects to server and authenticates. The server must
 * already be listed in the known_hosts-file.
 *
 * @param host Host address
 * @param user Username
 * @param keyfile Private key file
 * @param timeout Connection timeout
 * @return If success, the session. On error, an error message.
 */
std::tuple<SSession, std::string>
init_ssh_session(const std::string& host, const std::string& user, const std::string& keyfile,
                 std::chrono::milliseconds timeout);

struct CmdResult
{
    int         rc {-1};
    std::string output;
    std::string error_output;
    std::string ssh_error;
};
CmdResult run_cmd(ssh::Session& read_error_stream, const std::string& cmd, std::chrono::milliseconds timeout);
}
