/*
 * Copyright (c) 2023 MariaDB plc
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

#include <maxscale/monitorserver.hh>
#include <errmsg.h>
#include <mysqld_error.h>
#include <maxbase/format.hh>
#include <maxbase/json.hh>
#include <maxscale/diskspace.hh>
#include <maxscale/listener.hh>
#include <maxscale/paths.hh>
#include <maxscale/protocol/mariadb/diskspace.hh>
#include <maxscale/protocol/mariadb/maxscale.hh>
#include <maxscale/secrets.hh>
#include <maxscale/server.hh>
#include "internal/server.hh"

using std::string;

namespace
{
/** Server type specific bits */
const uint64_t server_type_bits = SERVER_MASTER | SERVER_SLAVE | SERVER_JOINED | SERVER_RELAY | SERVER_BLR;

/** All server bits */
const uint64_t all_server_bits = SERVER_RUNNING | SERVER_MAINT | SERVER_MASTER | SERVER_SLAVE
    | SERVER_JOINED | SERVER_RELAY | SERVER_BLR;

const char WRN_REQUEST_OVERWRITTEN[] =
    "Previous maintenance/draining request was not yet read by the monitor and was overwritten.";

const mxs::MonitorServer::EventList empty_event_list;

bool check_disk_space_exhausted(mxs::MonitorServer* pMs, const std::string& path,
                                const maxscale::disk::SizesAndName& san, int32_t max_percentage)
{
    bool disk_space_exhausted = false;
    int32_t used_percentage = ((san.total - san.available) / (double)san.total) * 100;

    if (used_percentage >= max_percentage)
    {
        MXB_ERROR("Disk space on %s at %s is exhausted; %d%% of the the disk mounted on the path %s has "
                  "been used, and the limit it %d%%.",
                  pMs->server->name(), pMs->server->address(), used_percentage, path.c_str(), max_percentage);
        disk_space_exhausted = true;
    }
    return disk_space_exhausted;
}

const std::map<mxs_monitor_event_t, const char*> event_names_map =
{
    {MASTER_DOWN_EVENT, "master_down", },
    {MASTER_UP_EVENT,   "master_up",   },
    {SLAVE_DOWN_EVENT,  "slave_down",  },
    {SLAVE_UP_EVENT,    "slave_up",    },
    {SERVER_DOWN_EVENT, "server_down", },
    {SERVER_UP_EVENT,   "server_up",   },
    {SYNCED_DOWN_EVENT, "synced_down", },
    {SYNCED_UP_EVENT,   "synced_up",   },
    {DONOR_DOWN_EVENT,  "donor_down",  },
    {DONOR_UP_EVENT,    "donor_up",    },
    {LOST_MASTER_EVENT, "lost_master", },
    {LOST_SLAVE_EVENT,  "lost_slave",  },
    {LOST_SYNCED_EVENT, "lost_synced", },
    {LOST_DONOR_EVENT,  "lost_donor",  },
    {NEW_MASTER_EVENT,  "new_master",  },
    {NEW_SLAVE_EVENT,   "new_slave",   },
    {NEW_SYNCED_EVENT,  "new_synced",  },
    {NEW_DONOR_EVENT,   "new_donor",   },
    {RELAY_UP_EVENT,    "relay_up",    },
    {RELAY_DOWN_EVENT,  "relay_down",  },
    {LOST_RELAY_EVENT,  "lost_relay",  },
    {NEW_RELAY_EVENT,   "new_relay",   },
    {BLR_UP_EVENT,      "blr_up",      },
    {BLR_DOWN_EVENT,    "blr_down",    },
    {LOST_BLR_EVENT,    "lost_blr",    },
    {NEW_BLR_EVENT,     "new_blr",     },
};
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

bool MonitorServer::connection_is_ok(ConnectResult connect_result)
{
    return connect_result == ConnectResult::OLDCONN_OK || connect_result == ConnectResult::NEWCONN_OK;
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

    // Also clear out the DNS lookup flag, the information stored in the file might not be in sync with the
    // configuration and the need for a DNS lookup might not be there.
    status &= ~SERVER_NEED_DNS;

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

    case MonitorServer::DNS_DONE:
        server->clear_status(SERVER_NEED_DNS);
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

bool MonitorServer::can_update_disk_space_status() const
{
    return m_ok_to_check_disk_space
           && (!m_shared.monitor_disk_limits.empty() || server->have_disk_space_limits());
}

void MariaServer::update_disk_space_status()
{
    if (const auto info = disk::get_info_by_path(con); info.has_value())
    {
        // Server-specific setting takes precedence.
        auto dst = server->get_disk_space_limits();
        if (dst.empty())
        {
            dst = m_shared.monitor_disk_limits;
        }

        bool disk_space_exhausted = false;
        int32_t star_max_percentage = -1;
        std::set<std::string> checked_paths;

        for (const auto& dst_item : dst)
        {
            const string& path = dst_item.first;
            int32_t max_percentage = dst_item.second;

            if (path == "*")
            {
                star_max_percentage = max_percentage;
            }
            else
            {
                auto j = info->find(path);

                if (j != info->end())
                {
                    const disk::SizesAndName& san = j->second;
                    disk_space_exhausted = check_disk_space_exhausted(this, path, san, max_percentage);
                    checked_paths.insert(path);
                }
                else
                {
                    MXB_WARNING("Disk space threshold specified for %s even though server %s at %s"
                                "does not have that.", path.c_str(), server->name(), server->address());
                }
            }
        }

        if (star_max_percentage != -1)
        {
            for (auto j = info->begin(); j != info->end(); ++j)
            {
                string path = j->first;
                if (checked_paths.find(path) == checked_paths.end())
                {
                    const disk::SizesAndName& san = j->second;
                    disk_space_exhausted = check_disk_space_exhausted(this, path, san, star_max_percentage);
                }
            }
        }

        if (disk_space_exhausted)
        {
            m_pending_status |= SERVER_DISK_SPACE_EXHAUSTED;
        }
        else
        {
            m_pending_status &= ~SERVER_DISK_SPACE_EXHAUSTED;
        }
    }
    else
    {
        SERVER* pServer = server;

        if (mysql_errno(con) == ER_UNKNOWN_TABLE)
        {
            // Disable disk space checking for this server.
            m_ok_to_check_disk_space = false;

            MXB_ERROR("Disk space cannot be checked for %s at %s, because either the version (%s) "
                      "is too old, or the DISKS information schema plugin has not been installed. "
                      "Disk space checking has been disabled.",
                      pServer->name(), pServer->address(), pServer->info().version_string());
        }
        else
        {
            MXB_ERROR("Checking the disk space for %s at %s failed due to: (%d) %s",
                      pServer->name(), pServer->address(), mysql_errno(con), mysql_error(con));
        }
    }
}

/**
 * Check if current monitored server status has changed.
 *
 * @return              true if status has changed
 */
bool MonitorServer::status_changed()
{
    return status_changed(m_prev_status, server->status());
}

// static
bool MonitorServer::status_changed(uint64_t before, uint64_t after)
{
    bool rval = false;

    /* Previous status is -1 if not yet set */
    if (before != static_cast<uint64_t>(-1))
    {

        uint64_t old_status = before & all_server_bits;
        uint64_t new_status = after & all_server_bits;

        /**
         * The state has changed if the relevant state bits are not the same,
         * the server is either running, stopping or starting and the server is
         * not going into maintenance or coming out of it
         */
        if (old_status != new_status
            && ((old_status | new_status) & SERVER_MAINT) == 0
            && ((old_status | new_status) & SERVER_RUNNING) == SERVER_RUNNING)
        {
            rval = true;
        }
    }

    return rval;
}

/*
 * Determine a monitor event, defined by the difference between the old
 * status of a server and the new status.
 *
 * @return  monitor_event_t     A monitor event (enum)
 *
 * @note This function must only be called from mon_process_state_changes
 */
mxs_monitor_event_t MonitorServer::get_event_type() const
{
    auto rval = event_type(m_prev_status, server->status());

    mxb_assert_message(rval != UNDEFINED_EVENT,
                       "No event for state transition: [%s] -> [%s]",
                       Target::status_to_string(m_prev_status, server->stats().n_current_conns()).c_str(),
                       server->status_string().c_str());

    return rval;
}

// static
mxs_monitor_event_t MonitorServer::event_type(uint64_t before, uint64_t after)
{
    typedef enum
    {
        DOWN_EVENT,
        UP_EVENT,
        LOSS_EVENT,
        NEW_EVENT,
        UNSUPPORTED_EVENT
    } general_event_type;

    general_event_type event_type = UNSUPPORTED_EVENT;

    uint64_t prev = before & all_server_bits;
    uint64_t present = after & all_server_bits;

    if (prev == present)
    {
        /* This should never happen */
        mxb_assert(false);
        return UNDEFINED_EVENT;
    }

    if ((prev & SERVER_RUNNING) == 0)
    {
        /* The server was not running previously */
        if ((present & SERVER_RUNNING) != 0)
        {
            event_type = UP_EVENT;
        }
        else
        {
            /* Otherwise, was not running and still is not running. This should never happen. */
            mxb_assert(false);
        }
    }
    else
    {
        /* Previous state must have been running */
        if ((present & SERVER_RUNNING) == 0)
        {
            event_type = DOWN_EVENT;
        }
        else
        {
            /** These are used to detect whether we actually lost something or
             * just transitioned from one state to another */
            uint64_t prev_bits = prev & (SERVER_MASTER | SERVER_SLAVE);
            uint64_t present_bits = present & (SERVER_MASTER | SERVER_SLAVE);

            /* Was running and still is */
            if ((!prev_bits || !present_bits || prev_bits == present_bits)
                && (prev & server_type_bits))
            {
                /* We used to know what kind of server it was */
                event_type = LOSS_EVENT;
            }
            else
            {
                /* We didn't know what kind of server it was, now we do*/
                event_type = NEW_EVENT;
            }
        }
    }

    mxs_monitor_event_t rval = UNDEFINED_EVENT;

    switch (event_type)
    {
    case UP_EVENT:
        rval = (present & SERVER_MASTER) ? MASTER_UP_EVENT :
            (present & SERVER_SLAVE) ? SLAVE_UP_EVENT :
            (present & SERVER_JOINED) ? SYNCED_UP_EVENT :
            (present & SERVER_RELAY) ? RELAY_UP_EVENT :
            (present & SERVER_BLR) ? BLR_UP_EVENT :
            SERVER_UP_EVENT;
        break;

    case DOWN_EVENT:
        rval = (prev & SERVER_MASTER) ? MASTER_DOWN_EVENT :
            (prev & SERVER_SLAVE) ? SLAVE_DOWN_EVENT :
            (prev & SERVER_JOINED) ? SYNCED_DOWN_EVENT :
            (prev & SERVER_RELAY) ? RELAY_DOWN_EVENT :
            (prev & SERVER_BLR) ? BLR_DOWN_EVENT :
            SERVER_DOWN_EVENT;
        break;

    case LOSS_EVENT:
        rval = (prev & SERVER_MASTER) ? LOST_MASTER_EVENT :
            (prev & SERVER_SLAVE) ? LOST_SLAVE_EVENT :
            (prev & SERVER_JOINED) ? LOST_SYNCED_EVENT :
            (prev & SERVER_RELAY) ? LOST_RELAY_EVENT :
            (prev & SERVER_BLR) ? LOST_BLR_EVENT :
            UNDEFINED_EVENT;
        break;

    case NEW_EVENT:
        rval = (present & SERVER_MASTER) ? NEW_MASTER_EVENT :
            (present & SERVER_SLAVE) ? NEW_SLAVE_EVENT :
            (present & SERVER_JOINED) ? NEW_SYNCED_EVENT :
            (present & SERVER_RELAY) ? NEW_RELAY_EVENT :
            (present & SERVER_BLR) ? NEW_BLR_EVENT :
            UNDEFINED_EVENT;
        break;

    default:
        /* This should never happen */
        mxb_assert(false);
        break;
    }

    return rval;
}

/**
 * Log an error about the failure to connect to a backend server and why it happened.
 *
 * @param rval Return value of mon_ping_or_connect_to_db
 */
void MonitorServer::log_connect_error(ConnectResult rval)
{
    mxb_assert(!connection_is_ok(rval));
    if (rval == ConnectResult::TIMEOUT)
    {
        MXB_ERROR("Monitor timed out when connecting to server %s[%s:%d] : '%s'",
                  server->name(), server->address(), server->port(), m_latest_error.c_str());
    }
    else
    {
        MXB_ERROR("Monitor was unable to connect to server %s[%s:%d] : '%s'",
                  server->name(), server->address(), server->port(), m_latest_error.c_str());
    }
}

void MonitorServer::log_state_change(const std::string& reason)
{
    string prev = Target::status_to_string(m_prev_status, server->stats().n_current_conns());
    string next = server->status_string();
    MXB_NOTICE("Server changed state: %s[%s:%u]: %s. [%s] -> [%s]%s%s",
               server->name(), server->address(), server->port(),
               get_event_name(), prev.c_str(), next.c_str(),
               reason.empty() ? "" : ": ", reason.c_str());
}

const char* MonitorServer::get_event_name()
{
    return get_event_name(last_event);
}

const char* MonitorServer::get_event_name(mxs_monitor_event_t event)
{
    auto it = event_names_map.find(event);
    mxb_assert(it != event_names_map.end());
    return it == event_names_map.end() ? "undefined_event" : it->second;
}

MariaServer::MariaServer(SERVER* server, const MonitorServer::SharedSettings& shared)
    : MonitorServer(server, shared)
{
}

MariaServer::~MariaServer()
{
    close_conn();
}

MonitorServer::ConnectResult MariaServer::ping_or_connect()
{
    auto old_type = server->info().type();
    auto connect = [this] {
        return ping_or_connect_to_db(m_shared.conn_settings, *server, &con, &m_latest_error);
    };

    auto res = connect();
    if (res == ConnectResult::NEWCONN_OK)
    {
        mxs_mysql_update_server_version(server, con);
        if (server->info().type() != old_type)
        {
            /**
             * The server type affects the init commands sent by mxs_mysql_real_connect.
             * If server type changed, reconnect so that the correct commands are sent.
             * This typically only happens during startup.
             */
            mysql_close(con);
            con = nullptr;
            res = connect();
        }
    }
    return res;
}

MonitorServer::ConnectResult
MariaServer::ping_or_connect_to_db(const MonitorServer::ConnectionSettings& sett, SERVER& server,
                                   MYSQL** ppConn, std::string* pError)
{
    mxb_assert(ppConn);
    mxb_assert(pError);
    auto pConn = *ppConn;
    if (pConn)
    {
        mxb::StopWatch timer;
        /** Return if the connection is OK */
        if (mysql_ping(pConn) == 0)
        {
            long time_us = std::chrono::duration_cast<std::chrono::microseconds>(timer.split()).count();
            server.set_ping(time_us);
            return ConnectResult::OLDCONN_OK;
        }
    }

    string uname = sett.username;
    string passwd = sett.password;
    const auto& srv = static_cast<const Server&>(server);           // Clean this up later.

    string server_specific_monuser = srv.monitor_user();
    if (!server_specific_monuser.empty())
    {
        uname = server_specific_monuser;
        passwd = srv.monitor_password();
    }

    auto dpwd = mxs::decrypt_password(passwd);

    auto connect = [&pConn, &sett, &server, &uname, &dpwd](int port) {
        if (pConn)
        {
            mysql_close(pConn);
        }
        pConn = mysql_init(nullptr);
        // ConC takes the timeouts in seconds.
        unsigned int conn_to_s = sett.connect_timeout.count();
        unsigned int read_to_s = sett.read_timeout.count();
        unsigned int write_to_s = sett.write_timeout.count();
        mysql_optionsv(pConn, MYSQL_OPT_CONNECT_TIMEOUT, &conn_to_s);
        mysql_optionsv(pConn, MYSQL_OPT_READ_TIMEOUT, &read_to_s);
        mysql_optionsv(pConn, MYSQL_OPT_WRITE_TIMEOUT, &write_to_s);
        mysql_optionsv(pConn, MYSQL_PLUGIN_DIR, mxs::connector_plugindir());
        mysql_optionsv(pConn, MARIADB_OPT_MULTI_STATEMENTS, nullptr);

        if (server.proxy_protocol())
        {
            mxq::set_proxy_header(pConn);
        }

        return mxs_mysql_real_connect(pConn, &server, port, uname.c_str(), dpwd.c_str()) != nullptr;
    };

    ConnectResult conn_result = ConnectResult::REFUSED;
    auto extra_port = server.extra_port();

    for (int i = 0; i < sett.connect_attempts && conn_result != ConnectResult::NEWCONN_OK; i++)
    {
        auto start = time(nullptr);
        if (extra_port > 0)
        {
            // Extra-port defined, try it first.
            if (connect(extra_port))
            {
                conn_result = ConnectResult::NEWCONN_OK;
            }
            else
            {
                // If extra-port connection failed due to too low max_connections or another likely
                // configuration related error, try normal port.
                auto err = mysql_errno(pConn);
                if (err == ER_CON_COUNT_ERROR || err == CR_CONNECTION_ERROR)
                {
                    if (connect(server.port()))
                    {
                        conn_result = ConnectResult::NEWCONN_OK;
                        MXB_WARNING("Could not connect with extra-port to '%s', using normal port.",
                                    server.name());
                    }
                }
            }
        }
        else if (connect(server.port()))
        {
            conn_result = ConnectResult::NEWCONN_OK;
        }

        if (conn_result == ConnectResult::REFUSED)
        {
            *pError = mysql_error(pConn);
            auto err = mysql_errno(pConn);
            mysql_close(pConn);
            pConn = nullptr;
            if (is_access_denied_error(err))
            {
                conn_result = ConnectResult::ACCESS_DENIED;
            }
            else if (difftime(time(nullptr), start) >= sett.connect_timeout.count())
            {
                conn_result = ConnectResult::TIMEOUT;
            }
        }
    }

    *ppConn = pConn;

    if (conn_result == ConnectResult::NEWCONN_OK)
    {
        // If a new connection was created, measure ping separately.
        mxb::StopWatch timer;
        long time_us = mxs::Target::PING_UNDEFINED;
        if (mysql_ping(pConn) == 0)
        {
            time_us = std::chrono::duration_cast<std::chrono::microseconds>(timer.split()).count();
        }
        server.set_ping(time_us);
    }

    return conn_result;
}

void MariaServer::close_conn()
{
    if (con)
    {
        mysql_close(con);
        con = nullptr;
    }
}

std::unique_ptr<mxb::QueryResult>
MariaServer::execute_query(const string& query, std::string* errmsg_out, unsigned int* errno_out)
{
    return maxscale::execute_query(con, query, errmsg_out, errno_out);
}

void MariaServer::check_permissions()
{
    // Test with a typical query to make sure the monitor has sufficient permissions.
    auto& query = permission_test_query();
    string err_msg;
    auto result = execute_query(query, &err_msg);

    if (result == nullptr)
    {
        /* In theory, this could be due to other errors as well, but that is quite unlikely since the
         * connection was just checked. The end result is in any case that the server is not updated,
         * and that this test is retried next round. */
        set_pending_status(SERVER_AUTH_ERROR);
        // Only print error if last round was ok.
        if (!had_status(SERVER_AUTH_ERROR))
        {
            MXB_WARNING("Error during monitor permissions test for server '%s': %s",
                        server->name(), err_msg.c_str());
        }
    }
    else
    {
        clear_pending_status(SERVER_AUTH_ERROR);
    }
}

const std::string& MariaServer::permission_test_query() const
{
    mxb_assert(!true);      // Can only be empty for monitors that do not check permissions.
    static string dummy = "";
    return dummy;
}

/**
 * Fetch variables from the server. The values are stored in the SERVER object.
 */
bool MariaServer::fetch_variables()
{
    bool rv = true;

    auto variables = server->tracked_variables();

    if (!variables.empty())
    {
        string query = "SHOW GLOBAL VARIABLES WHERE VARIABLE_NAME IN (" + mxb::join(variables, ",", "'")
            + ")";

        string err_msg;
        unsigned int err;
        if (auto r = mxs::execute_query(con, query, &err_msg, &err))
        {
            Server::Variables variable_values;
            while (r->next_row())
            {
                mxb_assert(r->get_col_count() == 2);

                auto variable = r->get_string(0);
                variable_values[variable] = r->get_string(1);

                variables.erase(variable);
            }

            if (mxb_log_should_log(LOG_INFO))
            {
                auto old_variables = server->get_variables();
                decltype(old_variables) changed;
                std::set_difference(variable_values.begin(), variable_values.end(),
                                    old_variables.begin(), old_variables.end(),
                                    std::inserter(changed, changed.begin()));

                if (!changed.empty())
                {
                    auto str = mxb::transform_join(changed, [](auto kv){
                        return kv.first + " = " + kv.second;
                    }, ", ", "'");

                    MXB_INFO("Variables have changed on '%s': %s", server->name(), str.c_str());
                }
            }

            bool changed = server->set_variables(std::move(variable_values));

            if (changed)
            {
                Listener::server_variables_changed(server);
            }

            if (!variables.empty())
            {
                MXB_INFO("Variable(s) %s were not found.", mxb::join(variables, ", ", "'").c_str());
            }
        }
        else
        {
            MXB_ERROR("Fetching server variables failed: (%d), %s", err, err_msg.c_str());
            mxb_assert(!strcasestr(err_msg.c_str(), "You have an error in your SQL syntax"));
            rv = false;
        }
    }

    return rv;
}

void MariaServer::fetch_uptime()
{
    if (auto r = mxs::execute_query(con, "SHOW STATUS LIKE 'Uptime'"))
    {
        if (r->next_row() && r->get_col_count() > 1)
        {
            server->set_uptime(r->get_int(1));
        }
    }
}
}
