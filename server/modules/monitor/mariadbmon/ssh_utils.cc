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

#include "ssh_utils.hh"
#include <utility>
#include <libssh/libsshpp.hpp>
#include <maxbase/format.hh>
#include <maxbase/stopwatch.hh>

using std::string;
using std::move;

namespace
{
std::tuple<ssh_key, std::string> read_private_key(const string& keyfile)
{
    ssh_key privkey {nullptr};
    string errmsg;
    int key_res = ssh_pki_import_privkey_file(keyfile.c_str(), nullptr, nullptr, nullptr, &privkey);
    if (key_res != SSH_OK)
    {
        if (key_res == SSH_EOF)
        {
            errmsg = mxb::string_printf("File does not exist or permission denied.");
        }
        else
        {
            // TODO: The library has a logging callback for more detailed error messages.
            errmsg = "Miscellaneous error.";
        }
    }
    return {privkey, errmsg};
}
}

namespace ssh_util
{

std::tuple<SSession, std::string>
init_ssh_session(const string& host, const string& user, const string& keyfile,
                 std::chrono::milliseconds timeout)
{
    auto [privkey, key_errmsg] = read_private_key(keyfile);
    if (!privkey)
    {
        return {nullptr,
                mxb::string_printf("Failed to read private key from file '%s'. %s",
                                   keyfile.c_str(), key_errmsg.c_str())};
    }

    SSession rval;
    string errmsg;

    try
    {
        auto ses = std::make_unique<ssh::Session>();
        ses->setOption(SSH_OPTIONS_HOST, host.c_str());
        ses->setOption(SSH_OPTIONS_USER, user.c_str());
        long timeout_ms = timeout.count();
        long timeout_s = timeout_ms / 1000;
        long timeout_us = 1000 * (timeout_ms % 1000);
        ses->setOption(SSH_OPTIONS_TIMEOUT, &timeout_s);
        ses->setOption(SSH_OPTIONS_TIMEOUT_USEC, &timeout_us);

        ses->connect();

        int pubkey_res = ses->isServerKnown();
        if (pubkey_res == SSH_KNOWN_HOSTS_OK)
        {
            // Server is known. Authenticate.
            ses->userauthPublickey(privkey);
            rval = move(ses);
        }
        else
        {
            // Maybe add ability to write pubkey to known_hosts.
            errmsg = "Public key of server was not found in known_hosts file.";
        }
    }
    catch (ssh::SshException& e)
    {
        errmsg = mxb::string_printf("Error %i: %s", e.getCode(), e.getError().c_str());
    }
    ssh_key_free(privkey);

    return {move(rval), move(errmsg)};
}

CmdResult run_cmd(ssh::Session& ses, const std::string& cmd, std::chrono::milliseconds timeout)
{
    CmdResult rval;
    mxb::StopWatch timer;

    // Open a channel and a channel session, then start command.
    ssh::Channel channel(ses);
    try
    {
        channel.openSession();
        channel.requestExec(cmd.c_str());

        auto read_output = [&](bool read_error_stream) {
            auto& output = read_error_stream ? rval.error_output : rval.output;

            size_t bufsize = 1024;
            char buf[bufsize];

            auto time_left = timeout - timer.split();
            int time_left_ms = std::chrono::duration_cast<std::chrono::milliseconds>(time_left).count();
            // LibSSH interprets negative timeouts as infinite.
            int time_left_eff_ms = std::max(time_left_ms, 1);

            // Errors throw, so no need to handle here.
            int read = channel.read(buf, bufsize, read_error_stream, time_left_eff_ms);
            if (read > 0)
            {
                output.append(buf, read);
                if ((size_t)read == bufsize)
                {
                    // If read the max amount, continue reading non-blocking.
                    do
                    {
                        read = channel.readNonblocking(buf, bufsize, read_error_stream);
                        if (read > 0)
                        {
                            output.append(buf, read);
                        }
                    }
                    while (read > 0);
                }
            }
        };

        bool keep_reading = true;
        while (keep_reading)
        {
            // Read both stdout and stderr.
            read_output(false);
            read_output(true);

            // Either all data was read or timed out. Check the time in case blocking read was interrupted.
            if (channel.isEof() || timer.split() > timeout)
            {
                keep_reading = false;
            }
        }

        channel.close();

        // Only read exit status if all output was read.
        if (channel.isEof())
        {
            rval.rc = channel.getExitStatus();      // This can block, deal with it later.
        }
    }
    catch (ssh::SshException& e)
    {
        rval.ssh_error = mxb::string_printf("Error %i: %s", e.getCode(), e.getError().c_str());
    }

    return rval;
}
}
