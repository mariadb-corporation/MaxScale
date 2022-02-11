/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/target.hh>
#include <maxscale/service.hh>
#include <maxscale/server.hh>

#include <mysqld_error.h>

const MXS_ENUM_VALUE rank_values[] =
{
    {"primary",   RANK_PRIMARY  },
    {"secondary", RANK_SECONDARY},
    {NULL}
};

const char* DEFAULT_RANK = "primary";

namespace maxscale
{

// static
Target* Target::find(const std::string& name)
{
    mxs::Target* rval = SERVER::find_by_unique_name(name.c_str());

    if (!rval)
    {
        rval = service_find(name.c_str());
    }

    return rval;
}

// static
std::string Target::status_to_string(uint64_t flags, int n_connections)
{
    std::string result;
    std::string separator;

    // Helper function.
    auto concatenate_if = [&result, &separator](bool condition, const std::string& desc) {
            if (condition)
            {
                result += separator + desc;
                separator = ", ";
            }
        };

    // TODO: The following values should be revisited at some point, but since they are printed by
    // the REST API they should not be changed suddenly. Strictly speaking, even the combinations
    // should not change, but this is more dependant on the monitors and have already changed.
    // Also, system tests compare to these strings so the output must stay constant for now.
    const std::string maintenance = "Maintenance";
    const std::string drained = "Drained";
    const std::string draining = "Draining";
    const std::string master = "Master";
    const std::string relay = "Relay Master";
    const std::string slave = "Slave";
    const std::string synced = "Synced";
    const std::string auth_err = "Auth Error";
    const std::string running = "Running";
    const std::string down = "Down";
    const std::string blr = "Binlog Relay";

    // Maintenance/Draining is usually set by user so is printed first.
    // Draining in the presence of Maintenance has no effect, so we only
    // print either one of those, with Maintenance taking precedence.
    if (status_is_in_maint(flags))
    {
        concatenate_if(true, maintenance);
    }
    else if (status_is_draining(flags))
    {
        if (n_connections == 0)
        {
            concatenate_if(true, drained);
        }
        else
        {
            concatenate_if(true, draining);
        }
    }

    // Master cannot be a relay or a slave.
    if (status_is_master(flags))
    {
        concatenate_if(true, master);
    }
    else
    {
        // Relays are typically slaves as well.
        concatenate_if(status_is_relay(flags), relay);
        concatenate_if(status_is_slave(flags), slave);
        concatenate_if(status_is_blr(flags), blr);
    }

    // The following Galera and Cluster bits may be combined with master/slave.
    concatenate_if(status_is_joined(flags), synced);

    concatenate_if(flags & SERVER_AUTH_ERROR, auth_err);
    concatenate_if(status_is_running(flags), running);
    concatenate_if(status_is_down(flags), down);

    return result;
}

void Target::response_time_add(double ave, int num_samples)
{
    /**
     * Apply backend average and adjust sample_max, which determines the weight of a new average
     * applied to EMAverage.
     *
     * Sample max is raised if the server is fast, aggressively lowered if the incoming average is clearly
     * lower than the EMA, else just lowered a bit. The normal increase and decrease, drifting, of the max
     * is done to follow the speed of a server. The important part is the lowering of max, to allow for a
     * server that is speeding up to be adjusted and used.
     *
     * Three new magic numbers to replace the sample max magic number... */
    constexpr double drift {1.1};
    std::lock_guard<std::mutex> guard(m_average_write_mutex);
    int current_max = m_response_time.sample_max();
    int new_max {0};

    // This server handles more samples than EMA max.
    // Increasing max allows all servers to be fairly compared.
    if (num_samples >= current_max)
    {
        new_max = num_samples * drift;
    }
    // This server is experiencing high load of some kind,
    // lower max to give more weight to the samples.
    else if (m_response_time.average() / ave > 2)
    {
        new_max = current_max * 0.5;
    }
    // Let the max slowly trickle down to keep
    // the sample max close to reality.
    else
    {
        new_max = current_max / drift;
    }

    m_response_time.set_sample_max(new_max);
    m_response_time.add(ave, num_samples);
}

void Target::set_rlag_state(RLagState new_state, int max_rlag)
{
    mxb_assert(new_state != RLagState::NONE);

    auto old_state = m_rlag_state.load(std::memory_order_relaxed);

    if (old_state != new_state
        && m_rlag_state.compare_exchange_strong(old_state, new_state,
                                                std::memory_order_acq_rel,
                                                std::memory_order_relaxed))
    {
        if (new_state == RLagState::ABOVE_LIMIT)
        {
            int64_t lag = replication_lag();

            if (lag != mxs::Target::RLAG_UNDEFINED)
            {
                MXS_WARNING("Replication lag of '%s' is %ld seconds, which is above the configured "
                            "limit %is. '%s' is excluded from query routing.", name(), lag, max_rlag, name());
            }
        }
        else if (old_state == RLagState::ABOVE_LIMIT)
        {
            MXS_WARNING("Replication lag of '%s' is %ld seconds, which is below the configured limit %is. "
                        "'%s' is returned to query routing.", name(), replication_lag(), max_rlag, name());
        }
    }
}

void Target::Stats::add_connection()
{
    // TODO: Looks a bit heavy to run every time a connection is made to server. The n_max_conns is only
    // shown to user, does it need to be accurate or is it required at all?
    m_n_total_conns.fetch_add(1, std::memory_order_relaxed);
    const auto val_after = m_n_current_conns.fetch_add(1, std::memory_order_relaxed) + 1;
    // Only update the max value if it's smaller than the new value. It's possible another thread
    // manages to update it while this thread is reading the value.
    auto old_max = m_n_max_conns.load(std::memory_order_acquire);
    while (val_after > old_max)
    {
        // Updates old_max if it's not equal to m_n_max_conns.
        if (m_n_max_conns.compare_exchange_weak(old_max, val_after, std::memory_order_acq_rel))
        {
            break;
        }
    }
}

void Target::Stats::remove_connection()
{
    MXB_AT_DEBUG(auto val_before = ) m_n_current_conns.fetch_sub(1, std::memory_order_relaxed);
    mxb_assert(val_before > 0);
}

int64_t Target::Stats::n_current_conns() const
{
    // The returned result is used in if-statements, so use acquire ordering.
    return m_n_current_conns.load(std::memory_order_acquire);
}

int64_t Target::Stats::n_total_conns() const
{
    return m_n_total_conns.load(std::memory_order_relaxed);
}

int64_t Target::Stats::add_conn_intent()
{
    return m_n_intended_conns.fetch_add(1, std::memory_order_acq_rel) + 1;
}

void Target::Stats::remove_conn_intent()
{
    m_n_intended_conns.fetch_sub(1, std::memory_order_release);
}

int64_t Target::Stats::n_conn_intents() const
{
    return m_n_intended_conns.load(std::memory_order_acquire);
}

void Target::Stats::add_client_connection()
{
    m_n_clients_conns.fetch_add(1, std::memory_order_relaxed);
}

void Target::Stats::remove_client_connection()
{
    MXB_AT_DEBUG(auto val_before = ) m_n_clients_conns.fetch_sub(1, std::memory_order_relaxed);
    mxb_assert(val_before > 0);
}

int64_t Target::Stats::n_client_conns() const
{
    return m_n_clients_conns.load(std::memory_order_relaxed);
}

void Target::Stats::add_failed_auth()
{
    m_failed_auths.fetch_add(1, std::memory_order_relaxed);
}

void Target::Stats::add_packet()
{
    m_n_packets.fetch_add(1, std::memory_order_relaxed);
}

void Target::Stats::add_current_op()
{
    m_n_current_ops.fetch_add(1, std::memory_order_relaxed);
}

void Target::Stats::remove_current_op()
{
    MXB_AT_DEBUG(auto val_before = ) m_n_current_ops.fetch_sub(1, std::memory_order_relaxed);
    mxb_assert(val_before > 0);
}

int64_t Target::Stats::n_current_ops() const
{
    return m_n_current_ops.load(std::memory_order_relaxed);
}

json_t* Target::Stats::to_json() const
{
    const auto relaxed = std::memory_order_relaxed;

    json_t* stats = json_object();
    json_object_set_new(stats, "connections", json_integer(n_current_conns()));
    json_object_set_new(stats, "total_connections", json_integer(n_total_conns()));
    json_object_set_new(stats, "max_connections", json_integer(m_n_max_conns.load(relaxed)));
    json_object_set_new(stats, "active_operations", json_integer(n_current_ops()));
    json_object_set_new(stats, "routed_packets", json_integer(m_n_packets.load(relaxed)));
    json_object_set_new(stats, "failed_auths", json_integer(m_failed_auths.load(relaxed)));
    return stats;
}

Error::operator bool() const
{
    return m_code != 0;
}

bool Error::is_rollback() const
{
    bool rv = false;

    if (m_code != 0)
    {
        mxb_assert(m_sql_state.length() == 5);
        // The 'sql_state' of all transaction rollbacks is "40XXX".
        if (m_sql_state[0] == '4' && m_sql_state[1] == '0')
        {
            rv = true;
        }
    }

    return rv;
}

bool Error::is_unexpected_error() const
{
    switch (m_code)
    {
    case ER_CONNECTION_KILLED:
    case ER_SERVER_SHUTDOWN:
    case ER_NORMAL_SHUTDOWN:
    case ER_SHUTDOWN_COMPLETE:
        return true;

    default:
        return false;
    }
}

uint32_t Error::code() const
{
    return m_code;
}

const std::string& Error::sql_state() const
{
    return m_sql_state;
}

const std::string& Error::message() const
{
    return m_message;
}

void Error::clear()
{
    m_code = 0;
    m_sql_state.clear();
    m_message.clear();
}

std::string Reply::describe() const
{
    std::ostringstream ss;

    if (is_complete())
    {
        if (error())
        {
            ss << "Error: " << error().code() << ", " << error().sql_state() << " " << error().message();
        }
        else if (is_ok())
        {
            ss << "OK: " << num_warnings() << " warnings";
        }
        else if (is_resultset())
        {
            ss << "Resultset: " << rows_read() << " rows";
        }
        else
        {
            // TODO: Is this really unknown?
            ss << "Unknown result type";
        }
    }
    else
    {
        ss << "Partial reply";
    }

    return ss.str();
}

ReplyState Reply::state() const
{
    return m_reply_state;
}

std::string Reply::to_string() const
{
    switch (m_reply_state)
    {
    case ReplyState::START:
        return "START";

    case ReplyState::DONE:
        return "DONE";

    case ReplyState::RSET_COLDEF:
        return "COLDEF";

    case ReplyState::RSET_COLDEF_EOF:
        return "COLDEF_EOF";

    case ReplyState::RSET_ROWS:
        return "ROWS";

    case ReplyState::PREPARE:
        return "PREPARE";

    default:
        mxb_assert(!true);
        return "UNKNOWN";
    }
}

uint8_t Reply::command() const
{
    return m_command;
}

const Error& Reply::error() const
{
    return m_error;
}

bool Reply::is_complete() const
{
    return m_reply_state == ReplyState::DONE;
}

bool Reply::has_started() const
{
    return m_reply_state != ReplyState::START && m_reply_state != ReplyState::DONE;
}

bool Reply::is_resultset() const
{
    return !m_field_counts.empty();
}

bool Reply::is_ok() const
{
    return m_is_ok && !is_resultset() && !error();
}

uint64_t Reply::rows_read() const
{
    return m_row_count;
}

uint16_t Reply::num_warnings() const
{
    return m_num_warnings;
}

uint16_t Reply::server_status() const
{
    return m_server_status;
}

uint64_t Reply::size() const
{
    return m_size;
}

const std::vector<uint64_t>& Reply::field_counts() const
{
    return m_field_counts;
}

uint32_t Reply::generated_id() const
{
    return m_generated_id;
}

uint16_t Reply::param_count() const
{
    return m_param_count;
}

std::string Reply::get_variable(const std::string& name) const
{
    auto it = m_variables.find(name);
    return it != m_variables.end() ? it->second : "";
}

void Reply::set_command(uint8_t command)
{
    m_command = command;
}

void Reply::set_reply_state(mxs::ReplyState state)
{
    m_reply_state = state;
}

void Reply::add_rows(uint64_t row_count)
{
    m_row_count += row_count;
}

void Reply::add_bytes(uint64_t size)
{
    m_size += size;
}

void Reply::add_field_count(uint64_t field_count)
{
    m_field_counts.push_back(field_count);
}

void Reply::set_generated_id(uint32_t id)
{
    m_generated_id = id;
}

void Reply::set_param_count(uint16_t count)
{
    m_param_count = count;
}

void Reply::set_is_ok(bool is_ok)
{
    m_is_ok = is_ok;
}

void Reply::set_variable(const std::string& key, const std::string& value)
{
    m_variables.insert(std::make_pair(key, value));
}

void Reply::set_num_warnings(uint16_t warnings)
{
    m_num_warnings = warnings;
}

void Reply::set_server_status(uint16_t status)
{
    m_server_status = status;
}

void Reply::clear()
{
    m_command = 0;
    m_reply_state = ReplyState::DONE;
    m_error.clear();
    m_row_count = 0;
    m_num_warnings = 0;
    m_size = 0;
    m_generated_id = 0;
    m_param_count = 0;
    m_is_ok = false;
    m_field_counts.clear();
    m_variables.clear();
}
}
