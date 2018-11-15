/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "mariadbmon.hh"

#include <inttypes.h>
#include <set>
#include <sstream>
#include <maxbase/stopwatch.hh>
#include <maxscale/clock.h>
#include <maxscale/mysql_utils.h>
#include <maxscale/utils.hh>

using std::string;
using std::unique_ptr;
using maxscale::string_printf;
using maxbase::StopWatch;
using maxbase::Duration;

static const char RE_ENABLE_FMT[] = "To re-enable automatic %s, manually set '%s' to 'true' "
                                    "for monitor '%s' via MaxAdmin or the REST API, or restart MaxScale.";
const char NO_SERVER[] = "Server '%s' is not monitored by '%s'.";
const char FAILOVER_OK[] = "Failover '%s' -> '%s' performed.";
const char FAILOVER_FAIL[] = "Failover '%s' -> '%s' failed.";
const char SWITCHOVER_OK[] = "Switchover '%s' -> '%s' performed.";
const char SWITCHOVER_FAIL[] = "Switchover %s -> %s failed";

/**
 * Run a manual switchover, promoting a new master server and demoting the existing master.
 *
 * @param promotion_server The server which should be promoted. If null, monitor will autoselect.
 * @param demotion_server The server which should be demoted. Can be null for autoselect, in which case
 * monitor will select the cluster master server. Otherwise must be a valid master server or a relay.
 * @param error_out Error output
 * @return True, if switchover was performed successfully
 */
bool MariaDBMonitor::manual_switchover(SERVER* promotion_server, SERVER* demotion_server, json_t** error_out)
{
    /* The server parameters may be null, in which case the monitor will autoselect.
     *
     * Manual commands (as well as automatic ones) are ran at the end of a normal monitor loop,
     * so server states can be assumed to be up-to-date.
     */
    bool switchover_done = false;
    auto op = switchover_prepare(promotion_server, demotion_server, Log::ON, error_out);
    if (op)
    {
        switchover_done = switchover_perform(*op);
        if (switchover_done)
        {
            MXS_NOTICE(SWITCHOVER_OK, op->demotion.target->name(), op->promotion.target->name());
        }
        else
        {
            string msg = string_printf(SWITCHOVER_FAIL,
                                       op->demotion.target->name(), op->promotion.target->name());
            bool failover_setting = config_get_bool(m_monitor->parameters, CN_AUTO_FAILOVER);
            if (failover_setting)
            {
                disable_setting(CN_AUTO_FAILOVER);
                msg += ", automatic failover has been disabled";
            }
            msg += ".";
            PRINT_MXS_JSON_ERROR(error_out, "%s", msg.c_str());
        }
    }
    else
    {
        PRINT_MXS_JSON_ERROR(error_out, "Switchover cancelled.");
    }
    return switchover_done;
}

bool MariaDBMonitor::manual_failover(json_t** output)
{
    bool failover_done = false;
    auto op = failover_prepare(Log::ON, output);
    if (op)
    {
        failover_done = failover_perform(*op);
        if (failover_done)
        {
            MXS_NOTICE(FAILOVER_OK, op->demotion_target->name(), op->promotion.target->name());
        }
        else
        {
            PRINT_MXS_JSON_ERROR(output, FAILOVER_FAIL,
                                 op->demotion_target->name(), op->promotion.target->name());
        }
    }
    else
    {
        PRINT_MXS_JSON_ERROR(output, "Failover cancelled.");
    }
    return failover_done;
}

bool MariaDBMonitor::manual_rejoin(SERVER* rejoin_server, json_t** output)
{
    bool rval = false;
    if (cluster_can_be_joined())
    {
        const char* rejoin_serv_name = rejoin_server->name;
        MXS_MONITORED_SERVER* mon_slave_cand = mon_get_monitored_server(m_monitor, rejoin_server);
        if (mon_slave_cand)
        {
            MariaDBServer* slave_cand = get_server_info(mon_slave_cand);

            if (server_is_rejoin_suspect(slave_cand, output))
            {
                string gtid_update_error;
                if (m_master->update_gtids(&gtid_update_error))
                {
                    string no_rejoin_reason;
                    if (slave_cand->can_replicate_from(m_master, &no_rejoin_reason))
                    {
                        ServerArray joinable_server;
                        joinable_server.push_back(slave_cand);
                        if (do_rejoin(joinable_server, output) == 1)
                        {
                            rval = true;
                            MXS_NOTICE("Rejoin performed.");
                        }
                        else
                        {
                            PRINT_MXS_JSON_ERROR(output, "Rejoin attempted but failed.");
                        }
                    }
                    else
                    {
                        PRINT_MXS_JSON_ERROR(output,
                                             "%s cannot replicate from cluster master %s: %s.",
                                             rejoin_serv_name, m_master->name(), no_rejoin_reason.c_str());
                    }
                }
                else
                {
                    PRINT_MXS_JSON_ERROR(output,
                                         "The GTIDs of master server %s could not be updated: %s",
                                         m_master->name(), gtid_update_error.c_str());
                }
            }   // server_is_rejoin_suspect has added any error messages to the output, no need to print here
        }
        else
        {
            PRINT_MXS_JSON_ERROR(output,
                                 "The given server '%s' is not monitored by this monitor.",
                                 rejoin_serv_name);
        }
    }
    else
    {
        const char BAD_CLUSTER[] = "The server cluster of monitor '%s' is not in a state valid for joining. "
                                   "Either it has no master or its gtid domain is unknown.";
        PRINT_MXS_JSON_ERROR(output, BAD_CLUSTER, m_monitor->name);
    }

    return rval;
}

/**
 * Reset replication of the cluster. Removes all slave connections and deletes binlogs. Then resets the
 * gtid sequence of the cluster to 0 and directs all servers to replicate from the given master.
 *
 * @param master_server Server to use as master
 * @param error_out Error output
 * @return True if operation was successful
 */
bool MariaDBMonitor::manual_reset_replication(SERVER* master_server, json_t** error_out)
{
    // This command is a hail-mary type, so no need to be that careful. Users are only supposed to run this
    // when replication is broken and they know the cluster is in sync.

    // If a master has been given, use that as the master. Otherwise autoselect.
    MariaDBServer* new_master = NULL;
    if (master_server)
    {
        MariaDBServer* new_master_cand = get_server(master_server);
        if (new_master_cand == NULL)
        {
            PRINT_MXS_JSON_ERROR(error_out, NO_SERVER, master_server->name, m_monitor->name);
        }
        else if (!new_master_cand->is_usable())
        {
            PRINT_MXS_JSON_ERROR(error_out,
                                 "Server '%s' is down or in maintenance and cannot be used as master.",
                                 new_master_cand->name());
        }
        else
        {
            new_master = new_master_cand;
        }
    }
    else
    {
        const char BAD_MASTER[] = "Could not autoselect new master for replication reset because %s";
        if (m_master == NULL)
        {
            PRINT_MXS_JSON_ERROR(error_out, BAD_MASTER, "the cluster has no master.");
        }
        else if (!m_master->is_usable())
        {
            PRINT_MXS_JSON_ERROR(error_out, BAD_MASTER, "the master is down or in maintenance.");
        }
        else
        {
            new_master = m_master;
        }
    }

    bool rval = false;
    if (new_master)
    {
        bool error = false;
        // Step 1: Gather the list of affected servers. If any operation on the servers fails,
        // the reset fails as well.
        ServerArray targets;
        for (MariaDBServer* server : m_servers)
        {
            if (server->is_usable())
            {
                targets.push_back(server);
            }
        }
        // The 'targets'-array cannot be empty, at least 'new_master' is there.
        MXB_NOTICE("Reseting replication on the following servers: %s. '%s' will be the new master.",
                   monitored_servers_to_string(targets).c_str(), new_master->name());

        // Helper function for running a command on all servers in the list.
        auto exec_cmd_on_array = [&error](const ServerArray& targets, const string& query,
                                          json_t** error_out) {
                if (!error)
                {
                    for (MariaDBServer* server : targets)
                    {
                        string error_msg;
                        if (!server->execute_cmd(query, &error_msg))
                        {
                            error = true;
                            PRINT_MXS_JSON_ERROR(error_out, "%s", error_msg.c_str());
                            break;
                        }
                    }
                }
            };

        // Step 2: Stop and reset all slave connections, even external ones.
        for (MariaDBServer* server : targets)
        {
            if (!server->reset_all_slave_conns(error_out))
            {
                error = true;
                break;
            }
        }

        // In theory, this is wrong if there are no slaves. Cluster is modified soon anyway.
        m_cluster_modified = true;

        // Step 3: Set read_only and disable events.
        exec_cmd_on_array(targets, "SET GLOBAL read_only=1;", error_out);
        if (!error)
        {
            MXB_NOTICE("read_only set on affected servers.");
            if (m_handle_event_scheduler)
            {
                for (MariaDBServer* server : targets)
                {
                    if (!server->disable_events(MariaDBServer::BinlogMode::BINLOG_OFF, error_out))
                    {
                        error = true;
                        break;
                    }
                }
            }
        }

        // Step 4: delete binary logs.
        exec_cmd_on_array(targets, "RESET MASTER;", error_out);
        if (!error)
        {
            MXB_NOTICE("Binary logs deleted (RESET MASTER) on affected servers.");
        }

        // Step 5: Set gtid_slave_pos on all servers. This is also sets gtid_current_pos since binary logs
        // have been deleted.
        if (!error)
        {
            string slave_pos = string_printf("%" PRIi64 "-%" PRIi64 "-0",
                                             new_master->m_gtid_domain_id, new_master->m_server_id);
            string set_slave_pos = string_printf("SET GLOBAL gtid_slave_pos='%s';", slave_pos.c_str());
            exec_cmd_on_array(targets, set_slave_pos, error_out);
            if (!error)
            {
                MXB_NOTICE("gtid_slave_pos set to '%s' on affected servers.", slave_pos.c_str());
            }
        }

        if (!error)
        {
            // Step 6: Enable writing and events on new master, add gtid event.
            string error_msg;
            if (new_master->execute_cmd("SET GLOBAL read_only=0;", &error_msg))
            {
                // Point of no return, perform later steps even if an error occurs.
                m_next_master = new_master;

                if (m_handle_event_scheduler)
                {
                    if (!new_master->enable_events(error_out))
                    {
                        error = true;
                        PRINT_MXS_JSON_ERROR(error_out, "Could not enable events on '%s': %s",
                                             new_master->name(), error_msg.c_str());
                    }
                }

                // Add an event to the new master so that it has a non-empty gtid_current_pos.
                if (!new_master->execute_cmd("FLUSH TABLES;", &error_msg))
                {
                    error = true;
                    PRINT_MXS_JSON_ERROR(error_out, "Could not add event to %s: %s",
                                         new_master->name(), error_msg.c_str());
                }

                // Step 7: Set all slaves to replicate from the master.
                // The following commands are only sent to slaves.
                auto location = std::find(targets.begin(), targets.end(), new_master);
                targets.erase(location);

                // TODO: the following call does stop slave & reset slave again. Fix this later, although it
                // doesn't cause error.
                ServerArray dummy;
                if ((size_t)redirect_slaves(new_master, targets, &dummy) == targets.size())
                {
                    // TODO: Properly check check slave IO/SQL threads.
                    MXS_NOTICE("All slaves redirected successfully.");
                }
                else
                {
                    error = true;
                    PRINT_MXS_JSON_ERROR(error_out,
                                         "Some servers were not redirected to '%s'.", new_master->name());
                }
            }
            else
            {
                error = true;
                PRINT_MXS_JSON_ERROR(error_out, "Could not enable writes on '%s': %s",
                                     new_master->name(), error_msg.c_str());
            }
        }

        if (error)
        {
            PRINT_MXS_JSON_ERROR(error_out, "Replication reset failed or succeeded only partially. "
                                            "Server cluster may be in an invalid state for replication.");
        }
        rval = !error;
    }
    return rval;
}

/**
 * Generate a CHANGE MASTER TO-query.
 *
 * @param master_host Master hostname/address
 * @param master_port Master port
 * @return Generated query
 */
string MariaDBMonitor::generate_change_master_cmd(const string& master_host, int master_port)
{
    std::stringstream change_cmd;
    change_cmd << "CHANGE MASTER TO MASTER_HOST = '" << master_host << "', ";
    change_cmd << "MASTER_PORT = " << master_port << ", ";
    change_cmd << "MASTER_USE_GTID = current_pos, ";
    change_cmd << "MASTER_USER = '" << m_replication_user << "', ";
    const char MASTER_PW[] = "MASTER_PASSWORD = '";
    const char END[] = "';";
#if defined (SS_DEBUG)
    std::stringstream change_cmd_nopw;
    change_cmd_nopw << change_cmd.str();
    change_cmd_nopw << MASTER_PW << "******" << END;
    MXS_DEBUG("Change master command is '%s'.", change_cmd_nopw.str().c_str());
#endif
    change_cmd << MASTER_PW << m_replication_password << END;
    return change_cmd.str();
}

/**
 * Redirects slaves to replicate from another master server.
 *
 * @param new_master The replication master
 * @param slaves An array of slaves
 * @param redirected_slaves A vector where to insert successfully redirected slaves.
 * @return The number of slaves successfully redirected.
 */
int MariaDBMonitor::redirect_slaves(MariaDBServer* new_master, const ServerArray& slaves,
                                    ServerArray* redirected_slaves)
{
    mxb_assert(redirected_slaves != NULL);
    MXS_NOTICE("Redirecting slaves to new master.");
    string change_cmd = generate_change_master_cmd(new_master->m_server_base->server->address,
                                                   new_master->m_server_base->server->port);
    int successes = 0;
    for (MariaDBServer* slave : slaves)
    {
        if (slave->redirect_one_slave(change_cmd))
        {
            successes++;
            redirected_slaves->push_back(slave);
        }
    }
    return successes;
}

/**
 * Redirect slave connections from the promotion target to replicate from the demotion target and vice versa.
 *
 * @param op Operation descriptor
 * @param redirected_to_promo Output for slaves successfully redirected to promotion target
 * @param redirected_to_demo Output for slaves successfully redirected to demotion target
 * @return The number of slaves successfully redirected
 */
int MariaDBMonitor::redirect_slaves_ex(GeneralOpData& general, OperationType type,
                                       const MariaDBServer* promotion_target,
                                       const MariaDBServer* demotion_target,
                                       ServerArray* redirected_to_promo, ServerArray* redirected_to_demo)
{
    mxb_assert(type == OperationType::SWITCHOVER || type == OperationType::FAILOVER);

    // Slaves of demotion target are redirected to promotion target.
    // Try to redirect even disconnected slaves.
    ServerArray redirect_to_promo_target = get_redirectables(demotion_target, promotion_target);
    // Slaves of promotion target are redirected to demotion target in case of switchover.
    // This list contains elements only when promoting a relay in switchover.
    ServerArray redirect_to_demo_target;
    if (type == OperationType::SWITCHOVER)
    {
        redirect_to_demo_target = get_redirectables(promotion_target, demotion_target);
    }
    if (redirect_to_promo_target.empty() && redirect_to_demo_target.empty())
    {
        // This is ok, nothing to do.
        return 0;
    }

    /* In complicated topologies, this redirection can get tricky. It's possible that a slave is
     * replicating from both promotion and demotion targets and with different settings. This leads
     * to a somewhat similar situation as in promotion (connection copy/merge).
     *
     * Neither slave connection can be redirected since they would be conflicting. As a temporary
     * solution, such duplicate slave connections are for now avoided by not redirecting them. If this
     * becomes an issue (e.g. connection settings need to be properly preserved), add code which:
     * 1) In switchover, swaps the connections by first deleting or redirecting the other to a nonsensial
     * host to avoid host:port conflict.
     * 2) In failover, deletes the connection to promotion target and redirects the one to demotion target,
     * or does the same as in 1.
     */

    const char redir_fmt[] = "Redirecting %s to replicate from %s instead of %s.";
    string slave_names_to_promo = monitored_servers_to_string(redirect_to_promo_target);
    string slave_names_to_demo = monitored_servers_to_string(redirect_to_demo_target);
    mxb_assert(slave_names_to_demo.empty() || type == OperationType::SWITCHOVER);

    // Print both name lists if both have items, otherwise just the one with items.
    if (!slave_names_to_promo.empty() && !slave_names_to_demo.empty())
    {
        MXS_NOTICE("Redirecting %s to replicate from %s instead of %s, and %s to replicate from "
                   "%s instead of %s.",
                   slave_names_to_promo.c_str(), promotion_target->name(), demotion_target->name(),
                   slave_names_to_demo.c_str(), demotion_target->name(), promotion_target->name());
    }
    else if (!slave_names_to_promo.empty())
    {
        MXS_NOTICE(redir_fmt,
                   slave_names_to_promo.c_str(), promotion_target->name(), demotion_target->name());
    }
    else if (!slave_names_to_demo.empty())
    {
        MXS_NOTICE(redir_fmt,
                   slave_names_to_demo.c_str(), demotion_target->name(), promotion_target->name());
    }

    int successes = 0;
    int fails = 0;
    int conflicts = 0;
    auto redirection_helper =
        [this, &general, &conflicts, &successes, &fails](ServerArray& redirect_these,
                                                    const MariaDBServer* from, const MariaDBServer* to,
                                                    ServerArray* redirected) {
            for (MariaDBServer* redirectable : redirect_these)
            {
                mxb_assert(redirected != NULL);
                /* If the connection exists, even if disconnected, don't redirect.
                 * Compare host:port, since that is how server detects duplicate connections.
                 * Ignore for now the possibility of different host:ports having same server id:s
                 * etc as such setups shouldn't try failover/switchover anyway. */
                auto existing_conn = redirectable->slave_connection_status_host_port(to);
                if (existing_conn)
                {
                    // Already has a connection to redirect target.
                    conflicts++;
                    MXS_WARNING("%s already has a slave connection to %s, connection to %s was "
                                "not redirected.",
                                redirectable->name(), to->name(), from->name());
                }
                else
                {
                    // No conflict, redirect as normal.
                    auto old_conn = redirectable->slave_connection_status(from);
                    if (redirectable->redirect_existing_slave_conn(general, *old_conn, to))
                    {
                        successes++;
                        redirected->push_back(redirectable);
                    }
                    else
                    {
                        fails++;
                    }
                }
            }
        };

    redirection_helper(redirect_to_promo_target, demotion_target, promotion_target, redirected_to_promo);
    redirection_helper(redirect_to_demo_target, promotion_target, demotion_target, redirected_to_demo);

    if (fails == 0 && conflicts == 0)
    {
        MXS_NOTICE("All redirects successful.");
    }
    else if (fails == 0)
    {
        MXS_NOTICE("%i slave connections were redirected while %i connections were ignored.",
                   successes, conflicts);
    }
    else
    {
        int total = fails + conflicts + successes;
        MXS_WARNING("%i redirects failed, %i slave connections ignored and %i redirects successful "
                    "out of %i.", fails, conflicts, successes, total);
    }
    return successes;
}
/**
 * Set the new master to replicate from the cluster external master.
 *
 * @param new_master The server being promoted
 * @param err_out Error output
 * @return True if new master accepted commands
 */
bool MariaDBMonitor::start_external_replication(MariaDBServer* new_master, json_t** err_out)
{
    bool rval = false;
    MYSQL* new_master_conn = new_master->m_server_base->con;
    string change_cmd = generate_change_master_cmd(m_external_master_host, m_external_master_port);
    if (mxs_mysql_query(new_master_conn, change_cmd.c_str()) == 0
        && mxs_mysql_query(new_master_conn, "START SLAVE;") == 0)
    {
        MXS_NOTICE("New master starting replication from external master %s:%d.",
                   m_external_master_host.c_str(),
                   m_external_master_port);
        rval = true;
    }
    else
    {
        PRINT_MXS_JSON_ERROR(err_out,
                             "Could not start replication from external master: '%s'.",
                             mysql_error(new_master_conn));
    }
    return rval;
}

/**
 * (Re)join given servers to the cluster. The servers in the array are assumed to be joinable.
 * Usually the list is created by get_joinable_servers().
 *
 * @param joinable_servers Which servers to rejoin
 * @param output Error output. Can be null.
 * @return The number of servers successfully rejoined
 */
uint32_t MariaDBMonitor::do_rejoin(const ServerArray& joinable_servers, json_t** output)
{
    SERVER* master_server = m_master->m_server_base->server;
    const char* master_name = master_server->name;
    uint32_t servers_joined = 0;
    if (!joinable_servers.empty())
    {
        for (MariaDBServer* joinable : joinable_servers)
        {
            const char* name = joinable->name();
            bool op_success = false;
            // Rejoin doesn't have its own time limit setting. Use switchover time limit for now since
            // the first phase of standalone rejoin is similar to switchover.
            maxbase::Duration time_limit((double)m_switchover_timeout);
            GeneralOpData general(m_replication_user, m_replication_password, output, time_limit);

            if (joinable->m_slave_status.empty())
            {
                // Assume that server is an old master which was failed over. Even if this is not really
                // the case, the following is unlikely to do damage.
                ServerOperation demotion(joinable, true, /* treat as old master */
                                         m_handle_event_scheduler, m_demote_sql_file, {} /* unused */);
                if (joinable->demote(general, demotion))
                {
                    MXS_NOTICE("Directing standalone server '%s' to replicate from '%s'.", name, master_name);
                    // A slave connection description is required. As this is the only connection, no name
                    // is required.
                    SlaveStatus new_conn;
                    new_conn.master_host = master_server->address;
                    new_conn.master_port = master_server->port;
                    op_success = joinable->create_start_slave(general, new_conn);
                }
                else
                {
                    PRINT_MXS_JSON_ERROR(output,
                                         "Failed to prepare (demote) standalone server %s for rejoin.", name);
                }
            }
            else
            {
                MXS_NOTICE("Server '%s' is replicating from a server other than '%s', "
                           "redirecting it to '%s'.",
                           name, master_name, master_name);
                // Multisource replication does not get to this point.
                mxb_assert(joinable->m_slave_status.size() == 1);
                op_success = joinable->redirect_existing_slave_conn(general, joinable->m_slave_status[0],
                                                                    m_master);
            }

            if (op_success)
            {
                servers_joined++;
                m_cluster_modified = true;
            }
        }
    }
    return servers_joined;
}

/**
 * Check if the cluster is a valid rejoin target.
 *
 * @return True if master and gtid domain are known
 */
bool MariaDBMonitor::cluster_can_be_joined()
{
    return m_master != NULL && m_master->is_master() && m_master_gtid_domain != GTID_DOMAIN_UNKNOWN;
}

/**
 * Scan the servers in the cluster and add (re)joinable servers to an array.
 *
 * @param output Array to save results to. Each element is a valid (re)joinable server according
 * to latest data.
 * @return False, if there were possible rejoinable servers but communications error to master server
 * prevented final checks.
 */
bool MariaDBMonitor::get_joinable_servers(ServerArray* output)
{
    mxb_assert(output);

    // Whether a join operation should be attempted or not depends on several criteria. Start with the ones
    // easiest to test. Go though all slaves and construct a preliminary list.
    ServerArray suspects;
    for (MariaDBServer* server : m_servers)
    {
        if (server_is_rejoin_suspect(server, NULL))
        {
            suspects.push_back(server);
        }
    }

    // Update Gtid of master for better info.
    bool comm_ok = true;
    if (!suspects.empty())
    {
        string gtid_update_error;
        if (m_master->update_gtids(&gtid_update_error))
        {
            for (size_t i = 0; i < suspects.size(); i++)
            {
                string rejoin_err_msg;
                if (suspects[i]->can_replicate_from(m_master, &rejoin_err_msg))
                {
                    output->push_back(suspects[i]);
                }
                else if (m_warn_cannot_rejoin)
                {
                    // Print a message explaining why an auto-rejoin is not done. Suppress printing.
                    MXS_WARNING("Automatic rejoin was not attempted on server '%s' even though it is a "
                                "valid candidate. Will keep retrying with this message suppressed for all "
                                "servers. Errors: \n%s",
                                suspects[i]->name(),
                                rejoin_err_msg.c_str());
                    m_warn_cannot_rejoin = false;
                }
            }
        }
        else
        {
            MXS_ERROR("The GTIDs of master server %s could not be updated while attempting an automatic "
                      "rejoin: %s", m_master->name(), gtid_update_error.c_str());
            comm_ok = false;
        }
    }
    else
    {
        m_warn_cannot_rejoin = true;
    }
    return comm_ok;
}

/**
 * Checks if a server is a possible rejoin candidate. A true result from this function is not yet sufficient
 * criteria and another call to can_replicate_from() should be made.
 *
 * @param rejoin_cand Server to check
 * @param output Error output. If NULL, no error is printed to log.
 * @return True, if server is a rejoin suspect.
 */
bool MariaDBMonitor::server_is_rejoin_suspect(MariaDBServer* rejoin_cand, json_t** output)
{
    bool is_suspect = false;
    if (rejoin_cand->is_usable() && !rejoin_cand->is_master())
    {
        // Has no slave connection, yet is not a master.
        if (rejoin_cand->m_slave_status.empty())
        {
            is_suspect = true;
        }
        // Or has existing slave connection ...
        else if (rejoin_cand->m_slave_status.size() == 1)
        {
            SlaveStatus* slave_status = &rejoin_cand->m_slave_status[0];
            // which is connected to master but it's the wrong one
            if (slave_status->slave_io_running == SlaveStatus::SLAVE_IO_YES
                && slave_status->master_server_id != m_master->m_server_id)
            {
                is_suspect = true;
            }
            // or is disconnected but master host or port is wrong.
            else if (slave_status->slave_io_running == SlaveStatus::SLAVE_IO_CONNECTING
                     && slave_status->slave_sql_running
                     && (slave_status->master_host != m_master->m_server_base->server->address
                         || slave_status->master_port != m_master->m_server_base->server->port))
            {
                is_suspect = true;
            }
        }

        if (output != NULL && !is_suspect)
        {
            /* User has requested a manual rejoin but with a server which has multiple slave connections or
             * is already connected or trying to connect to the correct master. TODO: Slave IO stopped is
             * not yet handled perfectly. */
            if (rejoin_cand->m_slave_status.size() > 1)
            {
                const char MULTI_SLAVE[] = "Server '%s' has multiple slave connections, cannot rejoin.";
                PRINT_MXS_JSON_ERROR(output, MULTI_SLAVE, rejoin_cand->name());
            }
            else
            {
                const char CONNECTED[] = "Server '%s' is already connected or trying to connect to the "
                                         "correct master server.";
                PRINT_MXS_JSON_ERROR(output, CONNECTED, rejoin_cand->name());
            }
        }
    }
    else if (output != NULL)
    {
        PRINT_MXS_JSON_ERROR(output, "Server '%s' is master or not running.", rejoin_cand->name());
    }
    return is_suspect;
}

/**
 * Performs switchover for a simple topology (1 master, N slaves, no intermediate masters). If an
 * intermediate step fails, the cluster may be left without a master and manual intervention is
 * required to fix things.
 *
 * @param op Operation descriptor
 * @return True if successful. If false, replication may be broken.
 */
bool MariaDBMonitor::switchover_perform(SwitchoverParams& op)
{
    mxb_assert(op.demotion.target && op.promotion.target);
    const OperationType type = OperationType::SWITCHOVER;
    MariaDBServer* const promotion_target = op.promotion.target;
    MariaDBServer* const demotion_target = op.demotion.target;
    json_t** const error_out = op.general.error_out;

    bool rval = false;
    // Step 1: Set read-only to on, flush logs, update gtid:s.
    if (demotion_target->demote(op.general, op.demotion))
    {
        m_cluster_modified = true;
        bool catchup_and_promote_success = false;
        StopWatch timer;
        // Step 2: Wait for the promotion target to catch up with the demotion target. Disregard the other
        // slaves of the promotion target to avoid needless waiting.
        // The gtid:s of the demotion target were updated at the end of demotion.
        if (promotion_target->catchup_to_master(op.general, demotion_target->m_gtid_binlog_pos))
        {
            MXS_INFO("Switchover: Catchup took %.1f seconds.", timer.lap().secs());
            // Step 3: On new master: remove slave connections, set read-only to OFF etc.
            if (promotion_target->promote(op.general, op.promotion, type, demotion_target))
            {
                // Point of no return. Even if following steps fail, do not try to undo.
                // Switchover considered at least partially successful.
                catchup_and_promote_success = true;
                rval = true;
                if (op.promotion.to_from_master)
                {
                    // Force a master swap on next tick.
                    m_next_master = promotion_target;
                }

                // Step 4: Start replication on old master and redirect slaves.
                ServerArray redirected_to_promo_target;
                if (demotion_target->copy_slave_conns(op.general, op.demotion.conns_to_copy,
                                                      promotion_target))
                {
                    redirected_to_promo_target.push_back(demotion_target);
                }
                else
                {
                    MXS_WARNING("Could not copy slave connections from %s to %s.",
                                promotion_target->name(), demotion_target->name());
                }
                ServerArray redirected_to_demo_target;
                redirect_slaves_ex(op.general, type, promotion_target, demotion_target,
                                   &redirected_to_promo_target, &redirected_to_demo_target);

                if (!redirected_to_promo_target.empty() || !redirected_to_demo_target.empty())
                {
                    timer.restart();
                    // Step 5: Finally, check that slaves are replicating.
                    wait_cluster_stabilization(op.general, redirected_to_promo_target, promotion_target);
                    wait_cluster_stabilization(op.general, redirected_to_demo_target, demotion_target);
                    auto step6_duration = timer.lap();
                    MXS_INFO("Switchover: slave replication confirmation took %.1f seconds with "
                             "%.1f seconds to spare.",
                             step6_duration.secs(), op.general.time_remaining.secs());
                }
            }
        }

        if (!catchup_and_promote_success)
        {
            // Step 2 or 3 failed, try to undo step 2.
            const char QUERY_UNDO[] = "SET GLOBAL read_only=0;";
            if (mxs_mysql_query(demotion_target->m_server_base->con, QUERY_UNDO) == 0)
            {
                PRINT_MXS_JSON_ERROR(error_out, "read_only disabled on server %s.", demotion_target->name());
            }
            else
            {
                PRINT_MXS_JSON_ERROR(error_out,
                                     "Could not disable read_only on server %s: '%s'.",
                                     demotion_target->name(),
                                     mysql_error(demotion_target->m_server_base->con));
            }

            // Try to reactivate external replication if any.
            if (m_external_master_port != PORT_UNKNOWN)
            {
                start_external_replication(promotion_target, error_out);
            }
        }
    }
    return rval;
}

/**
 * Performs failover for a simple topology (1 master, N slaves, no intermediate masters).
 *
 * @param op Operation descriptor
 * @return True if successful
 */
bool MariaDBMonitor::failover_perform(FailoverParams& op)
{
    mxb_assert(op.promotion.target && op.demotion_target);
    const OperationType type = OperationType::FAILOVER;
    MariaDBServer* const promotion_target = op.promotion.target;
    auto const demotion_target = op.demotion_target;

    bool rval = false;
    // Step 1: Stop and reset slave, set read-only to OFF.
    if (promotion_target->promote(op.general, op.promotion, type, demotion_target))
    {
        // Point of no return. Even if following steps fail, do not try to undo. Failover considered
        // at least partially successful.
        rval = true;
        m_cluster_modified = true;
        if (op.promotion.to_from_master)
        {
            // Force a master swap on next tick.
            m_next_master = promotion_target;
        }

        // Step 2: Redirect slaves.
        ServerArray redirected_slaves;
        redirect_slaves_ex(op.general, type, promotion_target, demotion_target, &redirected_slaves, NULL);
        if (!redirected_slaves.empty())
        {
            StopWatch timer;
            /* Step 3: Finally, check that slaves are connected to the new master. Even if
             * time is out at this point, wait_cluster_stabilization() will check the slaves
             * once so that latest status is printed. */
            wait_cluster_stabilization(op.general, redirected_slaves, promotion_target);
            MXS_INFO("Failover: slave replication confirmation took %.1f seconds with "
                     "%.1f seconds to spare.",
                     timer.lap().secs(), op.general.time_remaining.secs());
        }
    }
    return rval;
}

/**
 * Check that the given slaves are connected and replicating from the new master. Only checks
 * the SLAVE STATUS of the slaves.
 *
 * @param op Operation descriptor
 * @param redirected_slaves Slaves to check
 * @param new_master The target server of the slave connections
 */
void MariaDBMonitor::wait_cluster_stabilization(GeneralOpData& op, const ServerArray& redirected_slaves,
                                                const MariaDBServer* new_master)
{
    if (redirected_slaves.empty())
    {
        // No need to check anything or print messages.
        return;
    }

    maxbase::Duration& time_remaining = op.time_remaining;
    StopWatch timer;
    // Check all the servers in the list. Using a set because erasing from container.
    std::set<MariaDBServer*> unconfirmed(redirected_slaves.begin(), redirected_slaves.end());
    ServerArray successes;
    ServerArray repl_fails;
    ServerArray query_fails;
    bool time_is_up = false;    // Try at least once, even if time is up.

    while (!unconfirmed.empty() && !time_is_up)
    {
        auto iter = unconfirmed.begin();
        while (iter != unconfirmed.end())
        {
            MariaDBServer* slave = *iter;
            if (slave->do_show_slave_status())
            {
                auto slave_conn = slave->slave_connection_status_host_port(new_master);
                if (slave_conn == NULL)
                {
                    // Highly unlikely. Maybe someone just removed the slave connection after it was created.
                    MXS_WARNING("%s does not have a slave connection to %s although one should have "
                                "been created.",
                                slave->name(), new_master->name());
                    repl_fails.push_back(*iter);
                    iter = unconfirmed.erase(iter);
                }
                else if (slave_conn->slave_io_running == SlaveStatus::SLAVE_IO_YES
                         && slave_conn->slave_sql_running == true)
                {
                    // This slave has connected to master and replication seems to be ok.
                    successes.push_back(*iter);
                    iter = unconfirmed.erase(iter);
                }
                else if (slave_conn->slave_io_running == SlaveStatus::SLAVE_IO_NO)
                {
                    // IO error on slave
                    MXS_WARNING("%s cannot start replication because of IO thread error: '%s'.",
                                slave_conn->to_short_string().c_str(), slave_conn->last_error.c_str());
                    repl_fails.push_back(*iter);
                    iter = unconfirmed.erase(iter);
                }
                else if (slave_conn->slave_sql_running == false)
                {
                    // SQL error on slave
                    MXS_WARNING("%s cannot start replication because of SQL thread error: '%s'.",
                                slave_conn->to_short_string().c_str(), slave_conn->last_error.c_str());
                    repl_fails.push_back(*iter);
                    iter = unconfirmed.erase(iter);
                }
                else
                {
                    // Slave IO is still connecting, must wait.
                    ++iter;
                }
            }
            else
            {
                query_fails.push_back(*iter);
                iter = unconfirmed.erase(iter);
            }
        }

        time_remaining -= timer.lap();
        if (!unconfirmed.empty())
        {
            if (time_remaining.secs() > 0)
            {
                double standard_sleep = 0.5;    // In seconds.
                // If we have unconfirmed slaves and have time remaining, sleep a bit and try again.
                /* TODO: This sleep is kinda pointless, because whether or not replication begins,
                 * all operations for failover/switchover are complete. The sleep is only required to
                 * get correct messages to the user. Think about removing it, or shortening the maximum
                 * time of this function. */
                Duration sleep_time = (time_remaining.secs() > standard_sleep) ?
                    Duration(standard_sleep) : time_remaining;
                std::this_thread::sleep_for(sleep_time);
            }
            else
            {
                // Have undecided slaves and is out of time.
                time_is_up = true;
            }
        }
    }

    if (successes.size() == redirected_slaves.size())
    {
        // Complete success.
        MXS_NOTICE("All redirected slaves successfully started replication from %s.", new_master->name());
    }
    else
    {
        if (!successes.empty())
        {
            MXS_NOTICE("%s successfully started replication from %s.",
                       monitored_servers_to_string(successes).c_str(), new_master->name());
        }
        // Something went wrong.
        auto fails = query_fails.size() + repl_fails.size() + unconfirmed.size();
        const char MSG[] = "%lu slaves did not start replicating from %s. "
                           "%lu encountered an I/O or SQL error, %lu failed to reply and %lu did not "
                           "connect to %s within the time limit.";
        MXS_WARNING(MSG, fails, new_master->name(), repl_fails.size(), query_fails.size(),
                    unconfirmed.size(), new_master->name());
    }
    time_remaining -= timer.lap();
}

/**
 * Select a promotion target for failover/switchover. Looks at the slaves of 'demotion_target' and selects
 * the server with the most up-do-date event or, if events are equal, the one with the best settings and
 * status.
 *
 * @param demotion_target The former master server/relay
 * @param op Switchover or failover
 * @param log_mode Print log or operate silently
 * @param error_out Error output
 * @return The selected promotion target or NULL if no valid candidates
 */
MariaDBServer* MariaDBMonitor::select_promotion_target(MariaDBServer* demotion_target,
                                                       OperationType  op,
                                                       Log log_mode,
                                                       json_t** error_out)
{
    /* Select a new master candidate. Selects the one with the latest event in relay log.
     * If multiple slaves have same number of events, select the one with most processed events. */

    if (!demotion_target->m_node.children.empty())
    {
        if (log_mode == Log::ON)
        {
            MXS_NOTICE("Selecting a server to promote and replace '%s'. Candidates are: %s.",
                       demotion_target->name(),
                       monitored_servers_to_string(demotion_target->m_node.children).c_str());
        }
    }
    else
    {
        PRINT_ERROR_IF(log_mode,
                       error_out,
                       "'%s' does not have any slaves to promote.",
                       demotion_target->name());
        return NULL;
    }

    // Servers that cannot be selected because of exclusion, but seem otherwise ok.
    ServerArray valid_but_excluded;

    string all_reasons;
    DelimitedPrinter printer("\n");
    // The valid promotion candidates are the slaves replicating directly from the demotion target.
    ServerArray candidates;
    for (MariaDBServer* cand : demotion_target->m_node.children)
    {
        string reason;
        if (!cand->can_be_promoted(op, demotion_target, &reason))
        {
            string msg = string_printf("'%s' cannot be selected because %s", cand->name(), reason.c_str());
            printer.cat(all_reasons, msg);
        }
        else if (server_is_excluded(cand))
        {
            valid_but_excluded.push_back(cand);
            string msg = string_printf("'%s' cannot be selected because it is excluded.", cand->name());
            printer.cat(all_reasons, msg);
        }
        else
        {
            candidates.push_back(cand);
            // Print some warnings about the candidate server.
            if (log_mode == Log::ON)
            {
                cand->warn_replication_settings();
            }
        }
    }

    MariaDBServer* current_best = NULL;
    if (candidates.empty())
    {
        PRINT_ERROR_IF(log_mode,
                       error_out,
                       "No suitable promotion candidate found:\n%s",
                       all_reasons.c_str());
    }
    else
    {
        current_best = candidates.front();
        candidates.erase(candidates.begin());
        if (!all_reasons.empty() && log_mode == Log::ON)
        {
            MXS_WARNING("Some servers were disqualified for promotion:\n%s", all_reasons.c_str());
        }
    }

    // Check which candidate is best
    string current_best_reason;
    int64_t gtid_domain = m_master_gtid_domain;
    for (MariaDBServer* cand : candidates)
    {
        if (is_candidate_better(cand, current_best, demotion_target, gtid_domain, &current_best_reason))
        {
            // Select the server for promotion, for now.
            current_best = cand;
        }
    }

    // Check if any of the excluded servers would be better than the best candidate. Only print one item.
    if (log_mode == Log::ON)
    {
        for (MariaDBServer* excluded : valid_but_excluded)
        {
            const char* excluded_name = excluded->name();
            if (current_best == NULL)
            {
                const char EXCLUDED_ONLY_CAND[] = "Server '%s' is a viable choice for new master, "
                                                  "but cannot be selected as it's excluded.";
                MXS_WARNING(EXCLUDED_ONLY_CAND, excluded_name);
                break;
            }
            else if (is_candidate_better(excluded, current_best, demotion_target, gtid_domain))
            {
                // Print a warning if this server is actually a better candidate than the previous best.
                const char EXCLUDED_CAND[] = "Server '%s' is superior to current best candidate '%s', "
                                             "but cannot be selected as it's excluded. This may lead to "
                                             "loss of data if '%s' is ahead of other servers.";
                MXS_WARNING(EXCLUDED_CAND, excluded_name, current_best->name(), excluded_name);
                break;
            }
        }
    }

    if (current_best && log_mode == Log::ON)
    {
        // If there was a specific reason this server was selected, print it now. If the first candidate
        // was chosen (likely all servers were equally good), do not print.
        string msg = string_printf("Selected '%s'", current_best->name());
        msg += current_best_reason.empty() ? "." : (" because " + current_best_reason);
        MXS_NOTICE("%s", msg.c_str());
    }
    return current_best;
}

/**
 * Is the server in the excluded list
 *
 * @param server Server to test
 * @return True if server is in the excluded-list of the monitor.
 */
bool MariaDBMonitor::server_is_excluded(const MariaDBServer* server)
{
    for (MariaDBServer* excluded : m_excluded_servers)
    {
        if (excluded == server)
        {
            return true;
        }
    }
    return false;
}

/**
 * Is the candidate a better choice for master than the previous best?
 *
 * @param candidate_info Server info of new candidate
 * @param current_best_info Server info of current best choice
 * @param demotion_target Server which will be demoted
 * @param gtid_domain Which domain to compare
 * @param reason_out Why is the candidate better than current_best
 * @return True if candidate is better
 */
bool MariaDBMonitor::is_candidate_better(const MariaDBServer* candidate, const MariaDBServer* current_best,
                                         const MariaDBServer* demotion_target, uint32_t gtid_domain,
                                         std::string* reason_out)
{
    const SlaveStatus* cand_slave_conn = candidate->slave_connection_status(demotion_target);
    const SlaveStatus* curr_best_slave_conn = current_best->slave_connection_status(demotion_target);
    mxb_assert(cand_slave_conn && curr_best_slave_conn);

    uint64_t cand_io = cand_slave_conn->gtid_io_pos.get_gtid(gtid_domain).m_sequence;
    uint64_t curr_io = curr_best_slave_conn->gtid_io_pos.get_gtid(gtid_domain).m_sequence;
    string reason;
    bool is_better = false;
    // A slave with a later event in relay log is always preferred.
    if (cand_io > curr_io)
    {
        is_better = true;
        reason = "it has received more events.";
    }
    // If io sequences are identical ...
    else if (cand_io == curr_io)
    {
        uint64_t cand_processed = candidate->m_gtid_current_pos.get_gtid(gtid_domain).m_sequence;
        uint64_t curr_processed = current_best->m_gtid_current_pos.get_gtid(gtid_domain).m_sequence;
        // ... the slave with more events processed wins.
        if (cand_processed > curr_processed)
        {
            is_better = true;
            reason = "it has processed more events.";
        }
        // If gtid positions are identical ...
        else if (cand_processed == curr_processed)
        {
            bool cand_updates = candidate->m_rpl_settings.log_slave_updates;
            bool curr_updates = current_best->m_rpl_settings.log_slave_updates;
            // ... prefer a slave with log_slave_updates.
            if (cand_updates && !curr_updates)
            {
                is_better = true;
                reason = "it has 'log_slave_updates' on.";
            }
            // If both have log_slave_updates on ...
            else if (cand_updates && curr_updates)
            {
                bool cand_disk_ok = !server_is_disk_space_exhausted(candidate->m_server_base->server);
                bool curr_disk_ok = !server_is_disk_space_exhausted(current_best->m_server_base->server);
                // ... prefer a slave without disk space issues.
                if (cand_disk_ok && !curr_disk_ok)
                {
                    is_better = true;
                    reason = "it is not low on disk space.";
                }
            }
        }
    }

    if (reason_out && is_better)
    {
        *reason_out = reason;
    }
    return is_better;
}

/**
 * Check cluster and parameters for suitability to failover. Also writes found servers to output pointers.
 *
 * @param log_mode Logging mode
 * @param error_out Error output
 * @return Operation object if cluster is suitable and failover may proceed, or NULL on error
 */
unique_ptr<MariaDBMonitor::FailoverParams> MariaDBMonitor::failover_prepare(Log log_mode, json_t** error_out)
{
    // This function resembles 'switchover_prepare', but does not yet support manual selection.

    // Check that the cluster has a non-functional master server and that one of the slaves of
    // that master can be promoted. TODO: add support for demoting a relay server.
    MariaDBServer* demotion_target = NULL;
    // Autoselect current master as demotion target.
    string demotion_msg;
    if (m_master == NULL)
    {
        const char msg[] = "Can not select a demotion target for failover: cluster does not have a master.";
        PRINT_ERROR_IF(log_mode, error_out, msg);
    }
    else if (!m_master->can_be_demoted_failover(&demotion_msg))
    {
        const char msg[] = "Can not select '%s' as a demotion target for failover because %s";
        PRINT_ERROR_IF(log_mode, error_out, msg, m_master->name(), demotion_msg.c_str());
    }
    else
    {
        demotion_target = m_master;
    }

    MariaDBServer* promotion_target = NULL;
    if (demotion_target)
    {
        // Autoselect best server for promotion.
        MariaDBServer* promotion_candidate = select_promotion_target(demotion_target, OperationType::FAILOVER,
                                                                     log_mode, error_out);
        if (promotion_candidate)
        {
            promotion_target = promotion_candidate;
        }
        else
        {
            PRINT_ERROR_IF(log_mode, error_out, "Could not autoselect promotion target for failover.");
        }
    }

    bool gtid_ok = false;
    if (demotion_target)
    {
        gtid_ok = check_gtid_replication(log_mode, demotion_target, error_out);
    }

    unique_ptr<FailoverParams> rval;
    if (promotion_target && demotion_target && gtid_ok)
    {
        const SlaveStatus* slave_conn = promotion_target->slave_connection_status(demotion_target);
        mxb_assert(slave_conn);
        uint64_t events = promotion_target->relay_log_events(*slave_conn);
        if (events > 0)
        {
            // The relay log of the promotion target is not yet clear. This is not really an error,
            // but should be communicated to the user in the case of manual failover. For automatic
            // failover, it's best to just try again during the next monitor iteration. The difference
            // to a typical prepare-fail is that the relay log status should be logged
            // repeatedly since it is likely to change continuously.
            if (error_out || log_mode == Log::ON)
            {
                const char unproc_fmt[] =
                    "The relay log of '%s' has %" PRIu64
                    " unprocessed events (Gtid_IO_Pos: %s, Gtid_Current_Pos: %s).";
                string unproc_events = string_printf(unproc_fmt, promotion_target->name(), events,
                                                     slave_conn->gtid_io_pos.to_string().c_str(),
                                                     promotion_target->m_gtid_current_pos.to_string().c_str());
                if (error_out)
                {
                    /* Print a bit more helpful error for the user, goes to log too.
                     * This should be a very rare occurrence: either the dba managed to start failover
                     * really fast, or the relay log is massive. In the latter case it's ok
                     * that the monitor does not do the waiting since there  is no telling how long
                     * the wait will be. */
                    const char wait_relay_log[] =
                        "%s To avoid data loss, failover should be postponed until "
                        "the log has been processed. Please try again later.";
                    string error_msg = string_printf(wait_relay_log, unproc_events.c_str());
                    PRINT_MXS_JSON_ERROR(error_out, "%s", error_msg.c_str());
                }
                else if (log_mode == Log::ON)
                {
                    // For automatic failover the message is more typical. TODO: Think if this message should
                    // be logged more often.
                    MXS_WARNING("%s To avoid data loss, failover is postponed until the log "
                                "has been processed.", unproc_events.c_str());
                }
            }
        }
        else
        {
            // The Duration ctor taking a double interprets is as seconds.
            auto time_limit = maxbase::Duration((double)m_failover_timeout);
            bool promoting_to_master = (demotion_target == m_master);
            ServerOperation promotion(promotion_target, promoting_to_master,
                                         m_handle_event_scheduler, m_promote_sql_file,
                                         demotion_target->m_slave_status);
            GeneralOpData general(m_replication_user, m_replication_password, error_out, time_limit);
            rval.reset(new FailoverParams(promotion, demotion_target, general));
        }
    }
    return rval;
}

/**
 * Check if failover is required and perform it if so.
 */
void MariaDBMonitor::handle_auto_failover()
{
    if (m_master == NULL || m_master->is_running())
    {
        // No need for failover. This also applies if master is in maintenance, because that is a user
        // problem.
        m_warn_master_down = true;
        m_warn_failover_precond = true;
        return;
    }

    int master_down_count = m_master->m_server_base->mon_err_count;
    const MariaDBServer* connected_slave = NULL;
    Duration event_age;
    Duration delay_time;

    if (m_failcount > 1 && m_warn_master_down)
    {
        int monitor_passes = m_failcount - master_down_count;
        MXS_WARNING("Master has failed. If master status does not change in %d monitor passes, failover "
                    "begins.",
                    (monitor_passes > 1) ? monitor_passes : 1);
        m_warn_master_down = false;
    }
    // If master seems to be down, check if slaves are receiving events.
    else if (m_verify_master_failure
             && (connected_slave = slave_receiving_events(m_master, &event_age, &delay_time)) != NULL)
    {
        MXS_NOTICE("Slave %s is still connected to %s and received a new gtid or heartbeat event %.1f "
                   "seconds ago. Delaying failover for at least %.1f seconds.",
                   connected_slave->name(), m_master->name(), event_age.secs(), delay_time.secs());
    }
    else if (master_down_count >= m_failcount)
    {
        // Failover is required, but first we should check if preconditions are met.
        Log log_mode = m_warn_failover_precond ? Log::ON : Log::OFF;
        auto op = failover_prepare(log_mode, NULL);
        if (op)
        {
            m_warn_failover_precond = true;
            MXS_NOTICE("Performing automatic failover to replace failed master '%s'.", m_master->name());
            if (failover_perform(*op))
            {
                MXS_NOTICE(FAILOVER_OK, op->demotion_target->name(), op->promotion.target->name());
            }
            else
            {
                MXS_ERROR(FAILOVER_FAIL, op->demotion_target->name(), op->promotion.target->name());
                report_and_disable("failover", CN_AUTO_FAILOVER, &m_auto_failover);
            }
        }
        else
        {
            // Failover was not attempted because of errors, however these errors are not permanent.
            // Servers were not modified, so it's ok to try this again.
            if (m_warn_failover_precond)
            {
                MXS_WARNING("Not performing automatic failover. Will keep retrying with most error messages "
                            "suppressed.");
                m_warn_failover_precond = false;
            }
        }
    }
}

/**
 * Is the topology such that failover and switchover are supported, even if not required just yet?
 * Print errors and disable the settings if this is not the case.
 */
void MariaDBMonitor::check_cluster_operations_support()
{
    bool supported = true;
    DelimitedPrinter printer("\n");
    string all_reasons;
    // Currently, only simple topologies are supported. No Relay Masters or multiple slave connections.
    // Gtid-replication is required, and a server version which supports it.
    for (MariaDBServer* server : m_servers)
    {
        // Need to accept unknown versions here. Otherwise servers which are down when the monitor starts
        // would deactivate failover.
        if (server->m_version != MariaDBServer::version::UNKNOWN
            && server->m_version != MariaDBServer::version::MARIADB_100)
        {
            supported = false;
            auto reason = string_printf("The version of %s (%s) is not supported. Failover/switchover "
                                        "requires MariaDB 10.0.2 or later.",
                                        server->name(), server->m_server_base->server->version_string);
            printer.cat(all_reasons, reason);
        }

        if (server->is_usable() && !server->m_slave_status.empty())
        {
            for (const auto& slave_conn : server->m_slave_status)
            {
                if (slave_conn.slave_io_running == SlaveStatus::SLAVE_IO_YES
                    && slave_conn.slave_sql_running && slave_conn.gtid_io_pos.empty())
                {
                    supported = false;
                    auto reason = string_printf("%s is not using gtid-replication.",
                                                slave_conn.to_short_string().c_str());
                    printer.cat(all_reasons, reason);
                }
            }
        }
    }

    if (!supported)
    {
        const char PROBLEMS[] =
            "The backend cluster does not support failover/switchover due to the following reason(s):\n"
            "%s\n"
            "Automatic failover/switchover has been disabled. They should only be enabled "
            "after the above issues have been resolved.";
        string p1 = string_printf(PROBLEMS, all_reasons.c_str());
        string p2 = string_printf(RE_ENABLE_FMT, "failover", CN_AUTO_FAILOVER, m_monitor->name);
        string p3 = string_printf(RE_ENABLE_FMT, "switchover", CN_SWITCHOVER_ON_LOW_DISK_SPACE,
                                  m_monitor->name);
        string total_msg = p1 + " " + p2 + " " + p3;
        MXS_ERROR("%s", total_msg.c_str());

        if (m_auto_failover)
        {
            m_auto_failover = false;
            disable_setting(CN_AUTO_FAILOVER);
        }
        if (m_switchover_on_low_disk_space)
        {
            m_switchover_on_low_disk_space = false;
            disable_setting(CN_SWITCHOVER_ON_LOW_DISK_SPACE);
        }
    }
}

/**
 * Check if a slave is receiving events from master. Returns the first slave that is both
 * connected (or not realized the disconnect yet) and has an event more recent than
 * master_failure_timeout. The age of the event is written in 'event_age_out'.
 *
 * @param demotion_target The server whose slaves should be checked
 * @param event_age_out Output for event age
 * @return The first connected slave or NULL if none found
 */
const MariaDBServer* MariaDBMonitor::slave_receiving_events(const MariaDBServer* demotion_target,
                                                            Duration* event_age_out, Duration* delay_out)
{
    Duration event_timeout(static_cast<double>(m_master_failure_timeout));
    auto current_time = maxbase::Clock::now();
    maxbase::Clock::time_point recent_event_time = current_time - event_timeout;

    const MariaDBServer* connected_slave = NULL;
    for (MariaDBServer* slave : demotion_target->m_node.children)
    {
        const SlaveStatus* slave_conn = NULL;
        if (slave->is_running()
            && (slave_conn = slave->slave_connection_status(demotion_target)) != NULL
            && slave_conn->slave_io_running == SlaveStatus::SLAVE_IO_YES
            && slave_conn->last_data_time >= recent_event_time)
        {
            // The slave is still connected to the correct master and has received events. This means that
            // while MaxScale can't connect to the master, it's probably still alive.
            connected_slave = slave;
            auto latest_event_age = current_time - slave_conn->last_data_time;
            *event_age_out = latest_event_age;
            *delay_out = event_timeout - latest_event_age;
            break;
        }
    }
    return connected_slave;
}

/**
 * Check cluster and parameters for suitability to switchover. Also writes found servers to output pointers.
 *
 * @param promotion_server The server which should be promoted. If null, monitor will autoselect.
 * @param demotion_server The server which should be demoted. Can be null for autoselect.
 * @param log_mode Logging mode
 * @param error_out Error output
 * @return Operation object if cluster is suitable and switchover may proceed, or NULL on error
 */
unique_ptr<MariaDBMonitor::SwitchoverParams>
MariaDBMonitor::switchover_prepare(SERVER* promotion_server, SERVER* demotion_server, Log log_mode,
                                   json_t** error_out)
{
    // Check that both servers are ok if specified, or autoselect them. Demotion target must be checked
    // first since the promotion target depends on it.
    MariaDBServer* demotion_target = NULL;
    string demotion_msg;
    if (demotion_server)
    {
        // Manual select.
        MariaDBServer* demotion_candidate = get_server(demotion_server);
        if (demotion_candidate == NULL)
        {
            PRINT_ERROR_IF(log_mode, error_out, NO_SERVER, demotion_server->name, m_monitor->name);
        }
        else if (!demotion_candidate->can_be_demoted_switchover(&demotion_msg))
        {
            PRINT_ERROR_IF(log_mode,
                           error_out,
                           "'%s' is not a valid demotion target for switchover: %s",
                           demotion_candidate->name(),
                           demotion_msg.c_str());
        }
        else
        {
            demotion_target = demotion_candidate;
        }
    }
    else
    {
        // Autoselect current master as demotion target.
        if (m_master == NULL || !m_master->is_master())
        {
            const char msg[] = "Can not autoselect a demotion target for switchover: cluster does "
                               "not have a master.";
            PRINT_ERROR_IF(log_mode, error_out, msg);
        }
        else if (!m_master->can_be_demoted_switchover(&demotion_msg))
        {
            const char msg[] = "Can not autoselect '%s' as a demotion target for switchover because %s";
            PRINT_ERROR_IF(log_mode, error_out, msg, m_master->name(), demotion_msg.c_str());
        }
        else
        {
            demotion_target = m_master;
        }
    }

    const auto op_type = OperationType::SWITCHOVER;
    MariaDBServer* promotion_target = NULL;
    if (demotion_target)
    {
        string promotion_msg;
        if (promotion_server)
        {
            // Manual select.
            MariaDBServer* promotion_candidate = get_server(promotion_server);
            if (promotion_candidate == NULL)
            {
                PRINT_ERROR_IF(log_mode, error_out, NO_SERVER, promotion_server->name, m_monitor->name);
            }
            else if (!promotion_candidate->can_be_promoted(op_type, demotion_target, &promotion_msg))
            {
                const char msg[] = "'%s' is not a valid promotion target for switchover because %s";
                PRINT_ERROR_IF(log_mode, error_out, msg, promotion_candidate->name(), promotion_msg.c_str());
            }
            else
            {
                promotion_target = promotion_candidate;
            }
        }
        else
        {
            // Autoselect. More involved than the autoselecting the demotion target.
            MariaDBServer* promotion_candidate = select_promotion_target(demotion_target,
                                                                         op_type,
                                                                         log_mode,
                                                                         error_out);
            if (promotion_candidate)
            {
                promotion_target = promotion_candidate;
            }
            else
            {
                PRINT_ERROR_IF(log_mode, error_out, "Could not autoselect promotion target for switchover.");
            }
        }
    }

    bool gtid_ok = false;
    if (demotion_target)
    {
        gtid_ok = check_gtid_replication(log_mode, demotion_target, error_out);
    }

    unique_ptr<SwitchoverParams> rval;
    if (promotion_target && demotion_target && gtid_ok)
    {
        maxbase::Duration time_limit((double)m_switchover_timeout);
        bool master_swap = (demotion_target == m_master);
        ServerOperation promotion(promotion_target, master_swap, m_handle_event_scheduler,
                                  m_promote_sql_file, demotion_target->m_slave_status);
        ServerOperation demotion(demotion_target, master_swap, m_handle_event_scheduler,
                                 m_demote_sql_file, promotion_target->m_slave_status);
        GeneralOpData general(m_replication_user, m_replication_password, error_out, time_limit);
        rval.reset(new SwitchoverParams(promotion, demotion, general));
    }
    return rval;
}

void MariaDBMonitor::enforce_read_only_on_slaves()
{
    const char QUERY[] = "SET GLOBAL read_only=1;";
    for (MariaDBServer* server : m_servers)
    {
        if (server->is_slave() && !server->is_read_only()
            && (server->m_version != MariaDBServer::version::BINLOG_ROUTER))
        {
            MYSQL* conn = server->m_server_base->con;
            if (mxs_mysql_query(conn, QUERY) == 0)
            {
                MXS_NOTICE("read_only set to ON on server '%s'.", server->name());
            }
            else
            {
                MXS_ERROR("Setting read_only on server '%s' failed: '%s.", server->name(), mysql_error(conn));
            }
        }
    }
}

void MariaDBMonitor::handle_low_disk_space_master()
{
    if (m_master && m_master->is_master() && m_master->is_low_on_disk_space())
    {
        if (m_warn_switchover_precond)
        {
            MXS_WARNING("Master server '%s' is low on disk space. Attempting to switch it with a slave.",
                        m_master->name());
        }

        // Looks like the master should be swapped out. Before trying it, check if there is even
        // a likely valid slave to swap to.
        Log log_mode = m_warn_switchover_precond ? Log::ON : Log::OFF;
        auto op = switchover_prepare(NULL, m_master->m_server_base->server, log_mode, NULL);
        if (op)
        {
            m_warn_switchover_precond = true;
            bool switched = switchover_perform(*op);
            if (switched)
            {
                MXS_NOTICE(SWITCHOVER_OK, op->demotion.target->name(), op->promotion.target->name());
            }
            else
            {
                MXS_ERROR(SWITCHOVER_FAIL, op->demotion.target->name(), op->promotion.target->name());
                report_and_disable("switchover", CN_SWITCHOVER_ON_LOW_DISK_SPACE,
                                   &m_switchover_on_low_disk_space);
            }
        }
        else
        {
            // Switchover was not attempted because of errors, however these errors are not permanent.
            // Servers were not modified, so it's ok to try this again.
            if (m_warn_switchover_precond)
            {
                MXS_WARNING("Not performing automatic switchover. Will keep retrying with this message "
                            "suppressed.");
                m_warn_switchover_precond = false;
            }
        }
    }
    else
    {
        m_warn_switchover_precond = true;
    }
}

void MariaDBMonitor::handle_auto_rejoin()
{
    ServerArray joinable_servers;
    if (get_joinable_servers(&joinable_servers))
    {
        uint32_t joins = do_rejoin(joinable_servers, NULL);
        if (joins > 0)
        {
            MXS_NOTICE("%d server(s) redirected or rejoined the cluster.", joins);
        }
    }
    // get_joinable_servers prints an error if master is unresponsive
}

void MariaDBMonitor::report_and_disable(const string& operation, const string& setting_name,
                                        bool* setting_var)
{
    string p1 = string_printf("Automatic %s failed, disabling automatic %s.",
                              operation.c_str(),
                              operation.c_str());
    string p2 = string_printf(RE_ENABLE_FMT, operation.c_str(), setting_name.c_str(), m_monitor->name);
    string error_msg = p1 + " " + p2;
    MXS_ERROR("%s", error_msg.c_str());
    *setting_var = false;
    disable_setting(setting_name.c_str());
}

/**
 * Check that the slaves to demotion target are using gtid replication and that the gtid domain of the
 * cluster is defined. Only the slave connections to the demotion target are checked.
 *
 * @param log_mode Logging mode
 * @param demotion_target The server whose slaves should be checked
 * @param error_out Error output
 * @return True if gtid is used
 */
bool MariaDBMonitor::check_gtid_replication(Log log_mode, const MariaDBServer* demotion_target,
                                            json_t** error_out)
{
    bool gtid_domain_ok = false;
    if (m_master_gtid_domain == GTID_DOMAIN_UNKNOWN)
    {
        PRINT_ERROR_IF(log_mode,
                       error_out,
                       "Cluster gtid domain is unknown. This is usually caused by the cluster never "
                       "having a master server while MaxScale was running.");
    }
    else
    {
        gtid_domain_ok = true;
    }

    // Check that all slaves are using gtid-replication.
    bool gtid_ok = true;
    for (MariaDBServer* server : demotion_target->m_node.children)
    {
        auto sstatus = server->slave_connection_status(demotion_target);
        if (sstatus && sstatus->gtid_io_pos.empty())
        {
            PRINT_ERROR_IF(log_mode,
                           error_out,
                           "The slave connection '%s' -> '%s' is not using gtid replication.",
                           server->name(),
                           demotion_target->name());
            gtid_ok = false;
        }
    }

    return gtid_domain_ok && gtid_ok;
}

/**
 * List slaves which should be redirected to the new master.
 *
 * @param old_master The server whose slaves are listed
 * @param ignored_slave A slave which should not be listed even if otherwise valid
 * @return A list of slaves to redirect
 */
ServerArray MariaDBMonitor::get_redirectables(const MariaDBServer* old_master,
                                              const MariaDBServer* ignored_slave)
{
    ServerArray redirectable_slaves;
    for (MariaDBServer* slave : old_master->m_node.children)
    {
        if (slave->is_usable() && slave != ignored_slave)
        {
            auto sstatus = slave->slave_connection_status(old_master);
            if (sstatus && !sstatus->gtid_io_pos.empty())
            {
                redirectable_slaves.push_back(slave);
            }
        }
    }
    return redirectable_slaves;
}

MariaDBMonitor::SwitchoverParams::SwitchoverParams(const ServerOperation& promotion,
                                                   const ServerOperation& demotion,
                                                   const GeneralOpData& general)
    : promotion(promotion)
    , demotion(demotion)
    , general(general)
{
}

MariaDBMonitor::FailoverParams::FailoverParams(const ServerOperation& promotion,
                                               const MariaDBServer* demotion_target,
                                               const GeneralOpData& general)
    : promotion(promotion)
    , demotion_target(demotion_target)
    , general(general)
{
}