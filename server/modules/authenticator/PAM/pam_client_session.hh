/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "pam_auth_common.hh"
#include <cstdint>
#include <string>
#include <vector>
#include <maxbase/pam_utils.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>

namespace maxbase
{
class AsyncProcess;
}
class PamClientAuthenticator;
class MariaDBClientConnection;

class PipeWatcher : mxb::Pollable
{
public:
    PipeWatcher(MariaDBClientConnection& client, mxb::Worker* worker, int fd);
    ~PipeWatcher();

    bool     poll();
    bool     stop_poll();
    int      poll_fd() const override;
    uint32_t handle_poll_events(mxb::Worker* pWorker, uint32_t events, Context context) override;

private:
    MariaDBClientConnection& m_client;
    mxb::Worker*             m_worker {nullptr};
    int                      m_poll_fd {-1};
    bool                     m_polling {false};
};

/** Client authenticator PAM-specific session data */
class PamClientAuthenticator : public mariadb::ClientAuthenticator
{
public:
    using AuthMode = mxb::pam::AuthMode;
    PamClientAuthenticator(AuthSettings settings, const PasswordMap& backend_pwds,
                           MariaDBClientConnection& client,
                           std::unique_ptr<mxb::AsyncProcess> proc = nullptr);

    ExchRes exchange(GWBUF&& read_buffer, MYSQL_session* session, AuthenticationData& auth_data) override;
    AuthRes authenticate(MYSQL_session* session, AuthenticationData& auth_data) override;

private:
    ExchRes exchange_old(GWBUF&& buffer, MYSQL_session* session, AuthenticationData& auth_data);
    ExchRes exchange_suid(GWBUF&& buffer, MYSQL_session* session, AuthenticationData& auth_data);
    ExchRes process_suid_messages(MYSQL_session* ses);
    AuthRes authenticate_old(MYSQL_session* session, AuthenticationData& auth_data);
    AuthRes authenticate_suid(AuthenticationData& auth_data);
    GWBUF   create_auth_change_packet(std::string_view msg) const;
    GWBUF   create_2fa_prompt_packet(std::string_view msg) const;
    GWBUF   create_conv_packet(std::string_view msg) const;
    void    write_backend_tokens(const std::string& mapped_user, AuthenticationData& auth_data);

    enum class State
    {
        INIT,
        ASKED_FOR_PW,
        ASKED_FOR_2FA,
        PW_RECEIVED,
        SUID_WAITING_CONV,
        SUID_WAITING_CLIENT_REPLY,
        DONE
    };

    State              m_state {State::INIT};       /**< Authentication state */
    const AuthSettings m_settings;
    const PasswordMap& m_backend_pwds;

    MariaDBClientConnection&           m_client;
    std::unique_ptr<mxb::AsyncProcess> m_proc;
    std::unique_ptr<PipeWatcher>       m_watcher;

    std::string m_suid_msgs;    /**< Unprocessed messages from external suid process */
    std::string m_mapped_user;
    int         m_conv_msgs {0};
    bool        m_eof_received {false};
};
