/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include "server_utils.hh"

#include <algorithm>
#include <cinttypes>
#include <maxbase/format.hh>
#include <maxbase/assert.hh>
#include "mariadbserver.hh"

using std::string;
using std::move;
using maxbase::string_printf;

namespace
{
// Used for Slave_IO_Running
const char YES[] = "Yes";
const char PREPARING[] = "Preparing";
const char CONNECTING[] = "Connecting";
const char NO[] = "No";
}

SlaveStatus::SlaveStatus(const std::string& owner)
    : settings(owner)
{
}

string SlaveStatus::to_string() const
{
    // Print all of this on the same line to make things compact. Are the widths reasonable? The format is
    // not quite array-like since usually there is just one row. May be changed later.
    // Form the components of the line.
    string running_states = string_printf("%s/%s",
                                          slave_io_to_string(slave_io_running).c_str(),
                                          slave_sql_running ? "Yes" : "No");

    string rval = string_printf(
        "  Host: %22s, IO/SQL running: %7s, Master ID: %4ld, Gtid_IO_Pos: %s, R.Lag: %ld",
        settings.master_endpoint.to_string().c_str(),
        running_states.c_str(),
        master_server_id,
        gtid_io_pos.to_string().c_str(),
        seconds_behind_master);
    return rval;
}

std::string SlaveStatus::Settings::to_string() const
{
    if (name.empty())
    {
        return string_printf("Replica connection from %s to %s",
                             m_owner.c_str(), master_endpoint.to_string().c_str());
    }
    else
    {
        return string_printf("Replica connection '%s' from %s to %s",
                             name.c_str(), m_owner.c_str(), master_endpoint.to_string().c_str());
    }
}

json_t* SlaveStatus::to_json() const
{
    json_t* result = json_object();
    json_object_set_new(result, "connection_name", json_string(settings.name.c_str()));
    json_object_set_new(result, "master_host", json_string(settings.master_endpoint.host().c_str()));
    json_object_set_new(result, "master_port", json_integer(settings.master_endpoint.port()));
    json_object_set_new(result, "slave_io_running",
                        json_string(slave_io_to_string(slave_io_running).c_str()));
    json_object_set_new(result, "slave_sql_running", json_string(slave_sql_running ? "Yes" : "No"));
    json_object_set_new(result, "seconds_behind_master",
                        seconds_behind_master == mxs::Target::RLAG_UNDEFINED ? json_null() :
                        json_integer(seconds_behind_master));
    json_object_set_new(result, "master_server_id", json_integer(master_server_id));
    json_object_set_new(result, "last_io_error", json_string(last_io_error.c_str()));
    json_object_set_new(result, "last_sql_error", json_string(last_sql_error.c_str()));
    json_object_set_new(result, "gtid_io_pos", json_string(gtid_io_pos.to_string().c_str()));
    const char* gtid_mode_str = (settings.gtid_mode == Settings::GtidMode::SLAVE) ? "Slave_Pos" :
                                (settings.gtid_mode == Settings::GtidMode::CURRENT) ? "Current_Pos" : "No";
    json_object_set_new(result, "using_gtid", json_string(gtid_mode_str));
    if (master_server)
    {
        json_object_set_new(result, "master_server_name", json_string(master_server->name()));
    }
    return result;
}

bool SlaveStatus::equal(const SlaveStatus& rhs) const
{
    // Strictly speaking, the following should depend on the 'assume_unique_hostnames',
    // but the situations it would make a difference are so rare they can be ignored.
    return slave_io_running == rhs.slave_io_running
           && slave_sql_running == rhs.slave_sql_running
           && settings.master_endpoint == rhs.settings.master_endpoint
           && settings.name == rhs.settings.name
           && master_server_id == rhs.master_server_id;
}

SlaveStatus::slave_io_running_t SlaveStatus::slave_io_from_string(const std::string& str)
{
    slave_io_running_t rval = SLAVE_IO_NO;
    if (str == YES)
    {
        rval = SLAVE_IO_YES;
    }
    // Interpret "Preparing" as "Connecting". It's not quite clear if the master server id has been read
    // or if server versions between master and slave have been checked, so better be on the safe side.
    else if (str == CONNECTING || str == PREPARING)
    {
        rval = SLAVE_IO_CONNECTING;
    }
    else if (str != NO)
    {
        MXB_ERROR("Unexpected value for Slave_IO_Running: '%s'.", str.c_str());
    }
    return rval;
}

string SlaveStatus::slave_io_to_string(SlaveStatus::slave_io_running_t slave_io)
{
    string rval;
    switch (slave_io)
    {
    case SlaveStatus::SLAVE_IO_YES:
        rval = YES;
        break;

    case SlaveStatus::SLAVE_IO_CONNECTING:
        rval = CONNECTING;
        break;

    case SlaveStatus::SLAVE_IO_NO:
        rval = NO;
        break;

    default:
        mxb_assert(!false);
    }
    return rval;
}

bool SlaveStatus::should_be_copied(string* ignore_reason_out) const
{
    bool accepted = true;
    auto master_id = master_server_id;
    // The connection is only copied if it is running or at least has been seen running.
    // Also, target should not be this server.
    string ignore_reason;
    if (!slave_sql_running)
    {
        accepted = false;
        ignore_reason = "its replica sql thread is not running.";
    }
    else if (!seen_connected)
    {
        accepted = false;
        ignore_reason = "it has not been seen connected to master.";
    }
    else if (master_id <= 0)
    {
        accepted = false;
        ignore_reason = string_printf("its Master_Server_Id (%li) is invalid .", master_id);
    }

    if (!accepted)
    {
        *ignore_reason_out = ignore_reason;
    }
    return accepted;
}

SlaveStatus::Settings::Settings(string name, EndPoint target, GtidMode gtid_mode, string owner)
    : name(move(name))
    , master_endpoint(move(target))
    , gtid_mode(gtid_mode)
    , m_owner(move(owner))
{
}

SlaveStatus::Settings::Settings(const std::string& name, const SERVER* target, GtidMode gtid_mode)
    : Settings(name, EndPoint::replication_endpoint(*target), gtid_mode, "")
{
}

SlaveStatus::Settings::Settings(string owner)
    : m_owner(move(owner))
{
}

ServerOperation::ServerOperation(MariaDBServer* target, TargetType target_type,
                                 SlaveStatusArray conns_to_copy, EventNameSet events_to_enable)
    : target(target)
    , target_type(target_type)
    , conns_to_copy(move(conns_to_copy))
    , events_to_enable(move(events_to_enable))
{
}

ServerOperation::ServerOperation(MariaDBServer* target, TargetType target_type)
    : ServerOperation(target, target_type, SlaveStatusArray(), EventNameSet())
{
}

GeneralOpData::GeneralOpData(OpStart start, mxb::Json& error, maxbase::Duration time_remaining)
    : start(start)
    , error_out(error)
    , time_remaining(time_remaining)
{
}

EndPoint::EndPoint(const std::string& host, int port)
    : m_host(host, port)
{
}

EndPoint EndPoint::replication_endpoint(const SERVER& server)
{
    const char* priv_addr = server.private_address();
    const char* addr = *priv_addr ? priv_addr : server.address();
    return EndPoint(addr, server.port());
}

EndPoint::EndPoint()
    : EndPoint("", PORT_UNKNOWN)
{
}

bool EndPoint::operator==(const EndPoint& rhs) const
{
    return m_host.address() == rhs.m_host.address() && m_host.port() == rhs.m_host.port();
}

std::string EndPoint::to_string() const
{
    return mxb::string_printf("[%s]:%d", m_host.address().c_str(), m_host.port());
}

bool EndPoint::points_to_server(const SERVER& srv) const
{
    // Ports must match, and the address must match either server normal or private address.
    return (m_host.port() == srv.port())
           && (m_host.address() == srv.address() || m_host.address() == srv.private_address());
}

void ServerLock::set_status(Status new_status, int64_t owner_id)
{
    m_owner_id = (new_status == Status::UNKNOWN || new_status == Status::FREE) ? CONN_ID_UNKNOWN : owner_id;
    m_status = new_status;
}

int64_t ServerLock::owner() const
{
    return m_owner_id;
}

ServerLock::Status ServerLock::status() const
{
    return m_status;
}

bool ServerLock::operator==(const ServerLock& rhs) const
{
    return m_status == rhs.m_status && m_owner_id == rhs.m_owner_id && m_owner_id != CONN_ID_UNKNOWN;
}

bool ServerLock::is_free() const
{
    return m_status == Status::FREE;
}

int round_to_seconds(mxb::Duration dur)
{
    return mxb::to_secs(dur) + 0.5;
}
