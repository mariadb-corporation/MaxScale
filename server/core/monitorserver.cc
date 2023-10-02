/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/monitorserver.hh>
#include <mysqld_error.h>
#include <maxbase/format.hh>
#include <maxbase/json.hh>
#include <maxscale/server.hh>

using std::string;

namespace
{
const char WRN_REQUEST_OVERWRITTEN[] =
    "Previous maintenance/draining request was not yet read by the monitor and was overwritten.";

const mxs::MonitorServer::EventList empty_event_list;
}

namespace journal_fields
{
const char FIELD_NAME[] = "name";
const char FIELD_STATUS[] = "status";
}

namespace maxscale
{
MonitorServer::MonitorServer(SERVER* server, const SharedSettings& shared)
    : server(server)
    , m_shared(shared)
{
}

bool MonitorServer::is_database() const
{
    return server->info().is_database();
}

const MonitorServer::ConnectionSettings& MonitorServer::conn_settings() const
{
    return m_shared.conn_settings;
}

bool MonitorServer::is_access_denied_error(int64_t errornum)
{
    return errornum == ER_ACCESS_DENIED_ERROR || errornum == ER_ACCESS_DENIED_NO_PASSWORD_ERROR;
}

const MonitorServer::EventList& MonitorServer::new_custom_events() const
{
    return empty_event_list;
}

mxb::Json MonitorServer::journal_data() const
{
    mxb::Json rval;
    rval.set_string(journal_fields::FIELD_NAME, server->name());
    rval.set_int(journal_fields::FIELD_STATUS, server->status());
    return rval;
}

void MonitorServer::read_journal_data(const mxb::Json& data)
{
    uint64_t status = data.get_int(journal_fields::FIELD_STATUS);

    // Ignoring the AUTH_ERROR status causes the authentication error message to be logged every time MaxScale
    // is restarted. This should make it easier to spot authentication related problems during startup.
    status &= ~SERVER_AUTH_ERROR;

    m_prev_status = status;
    server->set_status(status);
}

void MonitorServer::stash_current_status()
{
    // Should be run at the start of a monitor tick to both prepare next pending status and save the previous
    // status.
    auto status = server->status();
    m_prev_status = status;
    m_pending_status = status;
}

void MonitorServer::set_pending_status(uint64_t bits)
{
    m_pending_status |= bits;
}

void MonitorServer::clear_pending_status(uint64_t bits)
{
    m_pending_status &= ~bits;
}

bool MonitorServer::has_status(uint64_t bits) const
{
    return (m_pending_status & bits) == bits;
}

bool MonitorServer::had_status(uint64_t bits) const
{
    return (m_prev_status & bits) == bits;
}

bool MonitorServer::flush_status()
{
    bool status_changed = false;
    if (m_pending_status != server->status())
    {
        server->assign_status(m_pending_status);
        status_changed = true;
    }
    return status_changed;
}

bool MonitorServer::maybe_fetch_variables()
{
    bool rv = false;
    if (should_fetch_variables())
    {
        rv = fetch_variables();
    }
    return rv;
}

bool MonitorServer::should_fetch_variables()
{
    // Only fetch variables from real servers.
    return is_database();
}

bool MonitorServer::auth_status_changed()
{
    uint64_t old_status = m_prev_status;
    uint64_t new_status = server->status();

    return old_status != static_cast<uint64_t>(-1) && old_status != new_status
           && (old_status & SERVER_AUTH_ERROR) != (new_status & SERVER_AUTH_ERROR);
}

void MonitorServer::add_status_request(StatusRequest request)
{
    int previous_request = m_status_request.exchange(request, std::memory_order_acq_rel);
    // Warn if the previous request hasn't been read.
    if (previous_request != NO_CHANGE)
    {
        MXB_WARNING(WRN_REQUEST_OVERWRITTEN);
    }
}

void MonitorServer::apply_status_requests()
{
    // The admin can only modify the [Maintenance] and [Drain] bits.
    int admin_msg = m_status_request.exchange(NO_CHANGE, std::memory_order_acq_rel);
    string msg;

    switch (admin_msg)
    {
    case MonitorServer::MAINT_ON:
        if (!server->is_in_maint())
        {
            msg = mxb::string_printf("Server '%s' is going into maintenance.", server->name());
        }
        server->set_status(SERVER_MAINT);
        break;

    case MonitorServer::MAINT_OFF:
        if (server->is_in_maint())
        {
            msg = mxb::string_printf("Server '%s' is coming out of maintenance.", server->name());
        }
        server->clear_status(SERVER_MAINT);
        break;

    case MonitorServer::DRAINING_ON:
        if (!server->is_in_maint() && !server->is_draining())
        {
            msg = mxb::string_printf("Server '%s' is being drained.", server->name());
        }
        server->set_status(SERVER_DRAINING);
        break;

    case MonitorServer::DRAINING_OFF:
        if (!server->is_in_maint() && server->is_draining())
        {
            msg = mxb::string_printf("Server '%s' is no longer being drained.", server->name());
        }
        server->clear_status(SERVER_DRAINING);
        break;

    case MonitorServer::NO_CHANGE:
        break;

    default:
        mxb_assert(!true);
    }

    if (!msg.empty())
    {
        MXB_NOTICE("%s", msg.c_str());
    }
}
}
