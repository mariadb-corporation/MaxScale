/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/target.hh>
#include <maxscale/service.hh>
#include <maxscale/server.hh>
#include <maxbase/pretty_print.hh>

#include <mysqld_error.h>

#include "internal/service.hh"

namespace maxscale
{

// static
Target* Target::find(const std::string& name)
{
    mxs::Target* rval = SERVER::find_by_unique_name(name.c_str());

    if (!rval)
    {
        rval = Service::find(name);
    }

    return rval;
}

// static
std::string Target::status_to_string(uint64_t flags, int n_connections)
{
    std::string result;
    std::string separator;

    // Helper function.
    auto concatenate_if = [&result, &separator](bool condition, const std::string_view& desc) {
        if (condition)
        {
            result += separator;
            result += desc;
            separator = ", ";
        }
    };

    // TODO: The following values should be revisited at some point, but since they are printed by
    // the REST API they should not be changed suddenly. Strictly speaking, even the combinations
    // should not change, but this is more dependant on the monitors and have already changed.
    // Also, system tests compare to these strings so the output must stay constant for now.

    //
    // NOTE: Do NOT change the order in which the values are evaluated. The system tests (possibly other
    // software as well) rely on both the state names as well as the order in which they appear.
    //

    // Maintenance/Draining is usually set by user so is printed first.
    // Draining in the presence of Maintenance has no effect, so we only
    // print either one of those, with Maintenance taking precedence.
    if (status_is_in_maint(flags))
    {
        concatenate_if(true, MAINTENANCE);
    }
    else if (status_is_draining(flags))
    {
        if (n_connections == 0)
        {
            concatenate_if(true, DRAINED);
        }
        else
        {
            concatenate_if(true, DRAINING);
        }
    }

    // Master cannot be a relay or a slave.
    if (status_is_master(flags))
    {
        concatenate_if(true, MASTER);
    }
    else
    {
        // Relays are typically slaves as well.
        concatenate_if(status_is_relay(flags), RELAY);
        concatenate_if(status_is_slave(flags), SLAVE);
        concatenate_if(status_is_blr(flags), BLR);
    }

    // The following Galera and Cluster bits may be combined with master/slave.
    concatenate_if(status_is_joined(flags), SYNCED);

    concatenate_if(flags & SERVER_AUTH_ERROR, AUTH_ERR);
    concatenate_if(flags & SERVER_NEED_DNS, NEED_DNS);
    concatenate_if(status_is_running(flags), RUNNING);
    concatenate_if(status_is_down(flags), DOWN);

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
                MXB_WARNING("Replication lag of '%s' is %ld seconds, which is above the configured "
                            "limit %is. '%s' is excluded from query routing.", name(), lag, max_rlag, name());
            }
        }
        else if (old_state == RLagState::ABOVE_LIMIT)
        {
            MXB_WARNING("Replication lag of '%s' is %ld seconds, which is below the configured limit %is. "
                        "'%s' is returned to query routing.", name(), replication_lag(), max_rlag, name());
        }
    }
}

Target::Packet Target::get_packet_type(MXS_SESSION* session, const GWBUF& buffer)
{
    Packet type = Packet::READ;

    if (rcap_type_required(session->capabilities(), RCAP_TYPE_QUERY_CLASSIFICATION))
    {
        const uint32_t read_only_types = mxs::sql::TYPE_READ
            | mxs::sql::TYPE_USERVAR_READ | mxs::sql::TYPE_SYSVAR_READ | mxs::sql::TYPE_GSYSVAR_READ;

        uint32_t type_mask = 0;

        auto* parser = session->client_connection()->parser();
        // TODO: These could be combined.
        if (parser->is_query(buffer) || parser->is_prepare(buffer))
        {
            type_mask = parser->get_type_mask(buffer);
        }

        auto is_read_only = !(type_mask & ~read_only_types);
        auto is_read_only_trx = session->protocol_data()->is_trx_read_only();

        if (!is_read_only && !is_read_only_trx)
        {
            type = Packet::WRITE;
        }
    }
    else if (status() & SERVER_MASTER)
    {
        type = Packet::WRITE;
    }

    return type;
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

json_t* Target::Stats::to_json() const
{
    const auto relaxed = std::memory_order_relaxed;
    auto rw_packets = m_n_rw_packets.load(relaxed);
    auto ro_packets = m_n_ro_packets.load(relaxed);

    json_t* stats = json_object();
    json_object_set_new(stats, "connections", json_integer(n_current_conns()));
    json_object_set_new(stats, "total_connections", json_integer(n_total_conns()));
    json_object_set_new(stats, "max_connections", json_integer(m_n_max_conns.load(relaxed)));
    json_object_set_new(stats, "active_operations", json_integer(n_current_ops()));
    json_object_set_new(stats, "routed_packets", json_integer(ro_packets + rw_packets));
    json_object_set_new(stats, "routed_writes", json_integer(rw_packets));
    json_object_set_new(stats, "routed_reads", json_integer(ro_packets));
    json_object_set_new(stats, "failed_auths", json_integer(m_failed_auths.load(relaxed)));
    return stats;
}

Reply::Error::operator bool() const
{
    return m_code != 0;
}

bool Reply::Error::is_rollback() const
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

bool Reply::Error::is_unexpected_error() const
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

uint32_t Reply::Error::code() const
{
    return m_code;
}

const std::string& Reply::Error::sql_state() const
{
    return m_sql_state;
}

const std::string& Reply::Error::message() const
{
    return m_message;
}

void Reply::Error::clear()
{
    m_code = 0;
    m_sql_state.clear();
    m_message.clear();
}

std::string Reply::describe() const
{
    std::string rval;

    if (is_complete())
    {
        if (error())
        {
            rval = mxb::cat("Error: ", std::to_string(error().code()), ", ", error().sql_state(),
                            " ", error().message());
        }
        else if (is_ok())
        {
            rval = mxb::cat("OK: ", std::to_string(num_warnings()), " warnings");
        }
        else if (is_resultset())
        {
            rval = mxb::cat("Resultset: ", std::to_string(rows_read()),
                            " rows in ", mxb::pretty_size(size()));
        }
        else
        {
            // TODO: Is this really unknown?
            rval = "Unknown result type";
        }
    }
    else
    {
        rval = "Partial reply";
    }

    return rval;
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

    case ReplyState::LOAD_DATA:
        return "LOAD_DATA";
    }

    return "UNKNOWN";
}

std::string_view Reply::get_variable(std::string_view name) const
{
    std::string_view rv;

    if (auto it = m_variables.find(name);  it != m_variables.end())
    {
        rv = it->second;
    }

    return rv;
}

void Reply::clear()
{
    m_command = 0;
    m_reply_state = ReplyState::DONE;
    m_error.clear();
    m_row_count = 0;
    m_num_warnings = 0;
    m_size = 0;
    m_upload_size = 0;
    m_affected_rows = 0;
    m_last_insert_id = 0;
    m_generated_id = 0;
    m_param_count = 0;
    m_server_status = NO_SERVER_STATUS;
    m_is_ok = false;
    m_multiresult = false;
    m_field_counts.clear();
    m_variables.clear();
    m_row_data.clear();
}
}
