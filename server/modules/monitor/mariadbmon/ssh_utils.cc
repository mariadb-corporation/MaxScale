/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "ssh_utils.hh"
#include <utility>
#include <libssh/libsshpp.hpp>
#include <libssh/sftp.h>
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
init_ssh_session(const string& host, int port, const string& user, const string& keyfile,
                 bool check_host, std::chrono::milliseconds timeout)
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
        ses->setOption(SSH_OPTIONS_PORT, &port);
        ses->setOption(SSH_OPTIONS_USER, user.c_str());
        long timeout_ms = timeout.count();
        long timeout_s = timeout_ms / 1000;
        long timeout_us = 1000 * (timeout_ms % 1000);
        ses->setOption(SSH_OPTIONS_TIMEOUT, &timeout_s);
        ses->setOption(SSH_OPTIONS_TIMEOUT_USEC, &timeout_us);

        ses->connect();

        bool host_ok = false;
        if (check_host)
        {
            int pubkey_res = ses->isServerKnown();
            if (pubkey_res == SSH_KNOWN_HOSTS_OK)
            {
                // Server is known. Authenticate.
                host_ok = true;
            }
            else
            {
                errmsg = "Public key of server was not found in known_hosts file. Either connect to the "
                         "server manually or disable key checking.";
            }
        }
        else
        {
            host_ok = true;
        }

        if (host_ok)
        {
            ses->userauthPublickey(privkey);
            rval = move(ses);
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
            rval.type = CmdResult::Type::OK;
        }
        else
        {
            rval.type = CmdResult::Type::TIMEOUT;
        }
    }
    catch (ssh::SshException& e)
    {
        rval.error_output = mxb::string_printf("Error %i: %s", e.getCode(), e.getError().c_str());
    }

    return rval;
}

AsyncCmd::AsyncCmd(std::shared_ptr<ssh::Session> ses, std::unique_ptr<ssh::Channel> chan)
    : m_ses(move(ses))
    , m_chan(move(chan))
    , m_status(Status::BUSY)
{
}

AsyncCmd::Status AsyncCmd::update_status()
{
    if (m_status != Status::BUSY)
    {
        return m_status;
    }

    auto read_output = [&](bool read_error) {
        auto& output = read_error ? m_error_output : m_output;
        size_t bufsize = 1024;
        char buf[bufsize];

        int read;
        do
        {
            read = m_chan->readNonblocking(buf, bufsize, read_error);
            if (read > 0)
            {
                output.append(buf, read);
            }
            // If read <= 0, nothing was available yet or remote end sent eof.
        }
        while (read > 0);
    };

    try
    {
        // Read all available data from both streams, then check for eof.
        read_output(false);
        read_output(true);

        if (m_chan->isEof())
        {
            m_chan->close();
            m_rc = m_chan->getExitStatus();
            m_status = Status::READY;
        }
    }
    catch (ssh::SshException& e)
    {
        m_error_output = mxb::string_printf("Error %i: %s", e.getCode(), e.getError().c_str());
        m_status = Status::SSH_FAIL;
    }

    return m_status;
}

AsyncCmd::~AsyncCmd()
{
    if (m_status == Status::BUSY)
    {
        // If the remote command did not complete, try sending a signal to it before disconnecting.
        // Typical commands terminate on disconnect, but "socat" will stay running. Sending a signal
        // increases the likelihood that the process will actually end. TODO: see if this actually does
        // anything.
        try
        {
            m_chan->requestSendSignal("KILL");
        }
        catch (ssh::SshException& e)
        {
        }
    }
}

const std::string& AsyncCmd::output() const
{
    return m_output;
}

const std::string& AsyncCmd::error_output() const
{
    return m_error_output;
}

int AsyncCmd::rc() const
{
    return m_rc;
}

std::tuple<std::unique_ptr<AsyncCmd>, std::string>
start_async_cmd(std::shared_ptr<ssh::Session> ses, const std::string& cmd)
{
    // Open a channel and a channel session, then start command.
    auto channel = std::make_unique<ssh::Channel>(*ses);
    try
    {
        channel->openSession();
        channel->requestExec(cmd.c_str());
        auto async_cmd = std::make_unique<AsyncCmd>(move(ses), move(channel));
        return {move(async_cmd), ""};
    }
    catch (ssh::SshException& e)
    {
        return {nullptr, mxb::string_printf("Error %i: %s", e.getCode(), e.getError().c_str())};
    }
}

string form_cmd_error_msg(const CmdResult& res, const char* cmd)
{
    using RType = CmdResult::Type;
    string rval;
    if (res.type == RType::OK)
    {
        if (res.rc == 0)
        {
            rval = mxb::string_printf("Command '%s' succeeded.", cmd);
        }
        else
        {
            rval = mxb::string_printf("Command '%s' failed with error %i: '%s'", cmd, res.rc,
                                      res.error_output.c_str());
        }
    }
    else if (res.type == RType::TIMEOUT)
    {
        rval = mxb::string_printf("Command '%s' timed out.", cmd);
    }
    else
    {
        rval = mxb::string_printf("Failed to send command '%s'. %s", cmd, res.error_output.c_str());
    }
    return rval;
}

std::tuple<std::unique_ptr<SFTPSession>, std::string> start_sftp_ses(std::shared_ptr<ssh::Session> ses)
{
    std::unique_ptr<SFTPSession> new_ses;
    string errmsg;

    sftp_session sftp_ses = sftp_new(ses->getCSession());
    if (sftp_ses)
    {
        int rc = sftp_init(sftp_ses);
        if (rc == SSH_OK)
        {
            new_ses = std::make_unique<SFTPSession>(std::move(ses), sftp_ses);
        }
        else
        {
            errmsg = mxb::string_printf("Error initializing SFTP session: code %d.",
                                        sftp_get_error(sftp_ses));
            sftp_free(sftp_ses);
        }
    }
    else
    {
        errmsg = mxb::string_printf("Error allocating SFTP session: %s", ssh_get_error(ses->getCSession()));
    }
    return {std::move(new_ses), std::move(errmsg)};
}

SFTPSession::SFTPSession(std::shared_ptr<ssh::Session> ses, sftp_session_struct* sftp)
    : m_ses(std::move(ses))
    , m_sftp(sftp)
{
}

SFTPSession::~SFTPSession()
{
    if (m_sftp)
    {
        sftp_free(m_sftp);
    }
}

std::tuple<std::vector<FileInfo>, std::string> SFTPSession::list_directory(const std::string& path)
{
    std::vector<FileInfo> rval;
    string errmsg;

    auto cses = m_ses->getCSession();
    sftp_dir dir = sftp_opendir(m_sftp, path.c_str());
    if (dir)
    {
        std::vector<FileInfo> dir_contents;
        sftp_attributes attr {nullptr};
        while ((attr = sftp_readdir(m_sftp, dir)) != nullptr)
        {
            auto attr_type = attr->type;
            auto type = (attr_type == SSH_FILEXFER_TYPE_REGULAR) ? FileType::REG :
                (attr_type == SSH_FILEXFER_TYPE_DIRECTORY) ? FileType::DIR : FileType::OTHER;

            FileInfo info {attr->name, attr->owner, attr->size, type};
            dir_contents.emplace_back(std::move(info));
            sftp_attributes_free(attr);
        }

        if (sftp_dir_eof(dir))
        {
            int rc = sftp_closedir(dir);
            if (rc == SSH_OK)
            {
                rval = std::move(dir_contents);
            }
            else
            {
                errmsg = mxb::string_printf("Can't close directory: %s", ssh_get_error(cses));
            }
        }
        else
        {
            errmsg = mxb::string_printf("Can't list directory: %s", ssh_get_error(cses));
            sftp_closedir(dir);
        }
    }
    else
    {
        errmsg = mxb::string_printf("Directory not opened: %s", ssh_get_error(cses));
    }

    return {std::move(rval), std::move(errmsg)};
}
}
