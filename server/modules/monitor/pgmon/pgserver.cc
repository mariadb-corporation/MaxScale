/*
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

#include "pgserver.hh"
#include "maxscale/secrets.hh"

using std::string;

PgServer::PgServer(SERVER* server, const SharedSettings& shared)
    : MonitorServer(server, shared)
{
}

maxscale::MonitorServer::ConnectResult PgServer::ping_or_connect()
{
    if (m_conn.is_open())
    {
        mxb::StopWatch timer;
        /** Return if the connection is OK */
        if (m_conn.ping())
        {
            long time_us = std::chrono::duration_cast<std::chrono::microseconds>(timer.split()).count();
            server->set_ping(time_us);
            return ConnectResult::OLDCONN_OK;
        }
    }

    const auto& mon_settings = conn_settings();
    string uname = mon_settings.username;
    string passwd = mon_settings.password;

    string server_specific_monuser = server->monitor_user();
    if (!server_specific_monuser.empty())
    {
        uname = server_specific_monuser;
        passwd = server->monitor_password();
    }
    auto dpwd = mxs::decrypt_password(passwd);

    auto& conn_settings = m_conn.connection_settings();
    conn_settings.user = uname;
    conn_settings.password = dpwd;
    conn_settings.connect_timeout = mon_settings.connect_timeout.count();
    conn_settings.read_timeout = mon_settings.read_timeout.count();
    conn_settings.write_timeout = mon_settings.write_timeout.count();
    conn_settings.ssl = server->ssl_config();

    auto res = ConnectResult::REFUSED;
    if (m_conn.open(server->address(), server->port(), "postgres"))
    {
        auto info = m_conn.get_version_info();
        server->set_version(SERVER::BaseType::POSTGRESQL, info.version, info.info, 0);
        // TODO: if init commands are ever added to PgSQL, reconnect here similar to MariaDB.
        res = ConnectResult::NEWCONN_OK;
    }
    else
    {
        // TODO: according to documentation, error messages may have linebreaks. Handle it later.
        const char* conn_err = m_conn.error();
        if (strcasestr(conn_err, "authentication failed") != nullptr
            || strcasestr(conn_err, "no pg_hba.conf entry for host") != nullptr)
        {
            res = ConnectResult::ACCESS_DENIED;
        }
        else if (strcasestr(conn_err, "Connection timed out") != nullptr)
        {
            res = ConnectResult::TIMEOUT;
        }
        m_latest_error = conn_err;
    }

    return res;
}

void PgServer::close_conn()
{
    m_conn.close();
}

void PgServer::fetch_uptime()
{
}

void PgServer::update_disk_space_status()
{
    m_ok_to_check_disk_space = false;
}

bool PgServer::fetch_variables()
{
    return true;
}

void PgServer::check_permissions()
{
}
