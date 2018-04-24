/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "mariadbmon.hh"

#include <inttypes.h>
#include <sstream>
#include <maxscale/clock.h>
#include <maxscale/mysql_utils.h>

using std::string;

static void print_redirect_errors(MariaDBServer* first_server, const ServerArray& servers,
                                  json_t** err_out);

bool MariaDBMonitor::manual_switchover(SERVER* new_master, SERVER* current_master, json_t** error_out)
{
    bool stopped = stop();
    if (stopped)
    {
        MXS_NOTICE("Stopped the monitor %s for the duration of switchover.", m_monitor_base->name);
    }
    else
    {
        MXS_NOTICE("Monitor %s already stopped, switchover can proceed.", m_monitor_base->name);
    }

    /* It's possible for either current_master, or both new_master & current_master to be NULL, which means
     * autoselect. Only autoselecting new_master is not possible. Autoselection will happen at the actual
     * switchover function. */
    MariaDBServer *found_new_master = NULL, *found_curr_master = NULL;
    auto ok_to_switch = switchover_check(new_master, current_master, &found_new_master, &found_curr_master,
                                         error_out);

    bool rval = false;
    if (ok_to_switch)
    {
        bool switched = do_switchover(&found_curr_master, &found_new_master, error_out);
        const char AUTOSELECT[] = "<autoselect>";
        const char* curr_master_name = found_curr_master ? found_curr_master->name() : AUTOSELECT;
        const char* new_master_name = found_new_master ? found_new_master->name() : AUTOSELECT;

        if (switched)
        {
            MXS_NOTICE("Switchover %s -> %s performed.", curr_master_name, new_master_name);
            rval = true;
        }
        else
        {
            string format = "Switchover %s -> %s failed";
            bool failover_setting = config_get_bool(m_monitor_base->parameters, CN_AUTO_FAILOVER);
            if (failover_setting)
            {
                disable_setting(CN_AUTO_FAILOVER);
                format += ", automatic failover has been disabled.";
            }
            format += ".";
            PRINT_MXS_JSON_ERROR(error_out, format.c_str(), curr_master_name, new_master_name);
        }
    }

    if (stopped)
    {
        MariaDBMonitor::start(m_monitor_base, m_monitor_base->parameters);
    }
    return rval;
}

bool MariaDBMonitor::manual_failover(json_t** output)
{
    bool stopped = stop();
    if (stopped)
    {
        MXS_NOTICE("Stopped monitor %s for the duration of failover.", m_monitor_base->name);
    }
    else
    {
        MXS_NOTICE("Monitor %s already stopped, failover can proceed.", m_monitor_base->name);
    }

    bool rv = true;
    rv = failover_check(output);
    if (rv)
    {
        rv = do_failover(output);
        if (rv)
        {
            MXS_NOTICE("Failover performed.");
        }
        else
        {
            PRINT_MXS_JSON_ERROR(output, "Failover failed.");
        }
    }

    if (stopped)
    {
        MariaDBMonitor::start(m_monitor_base, m_monitor_base->parameters);
    }
    return rv;
}

bool MariaDBMonitor::manual_rejoin(SERVER* rejoin_server, json_t** output)
{
    bool stopped = stop();
    if (stopped)
    {
        MXS_NOTICE("Stopped monitor %s for the duration of rejoin.", m_monitor_base->name);
    }
    else
    {
        MXS_NOTICE("Monitor %s already stopped, rejoin can proceed.", m_monitor_base->name);
    }

    bool rval = false;
    if (cluster_can_be_joined())
    {
        const char* rejoin_serv_name = rejoin_server->name;
        MXS_MONITORED_SERVER* mon_slave_cand = mon_get_monitored_server(m_monitor_base, rejoin_server);
        if (mon_slave_cand)
        {
            MariaDBServer* slave_cand = get_server_info(mon_slave_cand);

            if (server_is_rejoin_suspect(slave_cand, output))
            {
                if (m_master->update_gtids())
                {
                    if (slave_cand->can_replicate_from(m_master))
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
                        PRINT_MXS_JSON_ERROR(output, "Server '%s' cannot replicate from cluster master '%s' "
                                             "or it could not be queried.", rejoin_serv_name,
                                             m_master->name());
                    }
                }
                else
                {
                    PRINT_MXS_JSON_ERROR(output, "Cluster master '%s' gtid info could not be updated.",
                                         m_master->name());
                }
            } // server_is_rejoin_suspect has added any error messages to the output, no need to print here
        }
        else
        {
            PRINT_MXS_JSON_ERROR(output, "The given server '%s' is not monitored by this monitor.",
                                 rejoin_serv_name);
        }
    }
    else
    {
        const char BAD_CLUSTER[] = "The server cluster of monitor '%s' is not in a state valid for joining. "
                                   "Either it has no master or its gtid domain is unknown.";
        PRINT_MXS_JSON_ERROR(output, BAD_CLUSTER, m_monitor_base->name);
    }

    if (stopped)
    {
        MariaDBMonitor::start(m_monitor_base, m_monitor_base->parameters);
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
    change_cmd << "MASTER_PORT = " <<  master_port << ", ";
    change_cmd << "MASTER_USE_GTID = current_pos, ";
    change_cmd << "MASTER_USER = '" << m_replication_user << "', ";
    const char MASTER_PW[] = "MASTER_PASSWORD = '";
    const char END[] = "';";
#if defined(SS_DEBUG)
    std::stringstream change_cmd_nopw;
    change_cmd_nopw << change_cmd.str();
    change_cmd_nopw << MASTER_PW << "******" << END;;
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
    ss_dassert(redirected_slaves != NULL);
    MXS_NOTICE("Redirecting slaves to new master.");
    string change_cmd = generate_change_master_cmd(new_master->m_server_base->server->address,
                                                   new_master->m_server_base->server->port);
    int successes = 0;
    for (auto iter = slaves.begin(); iter != slaves.end(); iter++)
    {
        if ((*iter)->redirect_one_slave(change_cmd))
        {
            successes++;
            redirected_slaves->push_back(*iter);
        }
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
    if (mxs_mysql_query(new_master_conn, change_cmd.c_str()) == 0 &&
        mxs_mysql_query(new_master_conn, "START SLAVE;") == 0)
    {
        MXS_NOTICE("New master starting replication from external master %s:%d.",
                   m_external_master_host.c_str(), m_external_master_port);
        rval = true;
    }
    else
    {
        PRINT_MXS_JSON_ERROR(err_out, "Could not start replication from external master: '%s'.",
                             mysql_error(new_master_conn));
    }
    return rval;
}

/**
 * Starts a new slave connection on a server. Should be used on a demoted master server.
 *
 * @param old_master The server which will start replication
 * @param new_master Replication target
 * @return True if commands were accepted. This does not guarantee that replication proceeds
 * successfully.
 */
bool MariaDBMonitor::switchover_start_slave(MariaDBServer* old_master, MariaDBServer* new_master)
{
    bool rval = false;
    MYSQL* old_master_con = old_master->m_server_base->con;
    SERVER* new_master_server = new_master->m_server_base->server;

    string change_cmd = generate_change_master_cmd(new_master_server->address, new_master_server->port);
    if (mxs_mysql_query(old_master_con, change_cmd.c_str()) == 0 &&
        mxs_mysql_query(old_master_con, "START SLAVE;") == 0)
    {
        MXS_NOTICE("Old master '%s' starting replication from '%s'.",
                   old_master->name(), new_master->name());
        rval = true;
    }
    else
    {
        MXS_ERROR("Old master '%s' could not start replication: '%s'.",
                  old_master->name(), mysql_error(old_master_con));
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
        string change_cmd = generate_change_master_cmd(master_server->address, master_server->port);
        for (auto iter = joinable_servers.begin(); iter != joinable_servers.end(); iter++)
        {
            MariaDBServer* joinable = *iter;
            const char* name = joinable->name();

            bool op_success = false;
            if (joinable->m_slave_status.empty())
            {
                if (!m_demote_sql_file.empty() && !joinable->run_sql_from_file(m_demote_sql_file, output))
                {
                    PRINT_MXS_JSON_ERROR(output, "%s execution failed when attempting to rejoin server '%s'.",
                                         CN_DEMOTION_SQL_FILE, joinable->name());
                }
                else
                {
                    MXS_NOTICE("Directing standalone server '%s' to replicate from '%s'.", name, master_name);
                    op_success = joinable->join_cluster(change_cmd);
                }
            }
            else
            {
                MXS_NOTICE("Server '%s' is replicating from a server other than '%s', "
                           "redirecting it to '%s'.", name, master_name, master_name);
                op_success = joinable->redirect_one_slave(change_cmd);
            }

            if (op_success)
            {
                servers_joined++;
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
    return (m_master != NULL && m_master->is_master() && m_master_gtid_domain >= 0);
}

/**
 * Scan the servers in the cluster and add (re)joinable servers to an array.
 *
 * @param mon Cluster monitor
 * @param output Array to save results to. Each element is a valid (re)joinable server according
 * to latest data.
 * @return False, if there were possible rejoinable servers but communications error to master server
 * prevented final checks.
 */
bool MariaDBMonitor::get_joinable_servers(ServerArray* output)
{
    ss_dassert(output);

    // Whether a join operation should be attempted or not depends on several criteria. Start with the ones
    // easiest to test. Go though all slaves and construct a preliminary list.
    ServerArray suspects;
    for (auto iter = m_servers.begin(); iter != m_servers.end(); iter++)
    {
        if (server_is_rejoin_suspect(*iter, NULL))
        {
            suspects.push_back(*iter);
        }
    }

    // Update Gtid of master for better info.
    bool comm_ok = true;
    if (!suspects.empty())
    {
        if (m_master->update_gtids())
        {
            for (size_t i = 0; i < suspects.size(); i++)
            {
                if (suspects[i]->can_replicate_from(m_master))
                {
                    output->push_back(suspects[i]);
                }
            }
        }
        else
        {
            comm_ok = false;
        }
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
    if (rejoin_cand->is_running() && !rejoin_cand->is_master())
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
            if (slave_status->slave_io_running == SlaveStatus::SLAVE_IO_YES  &&
                slave_status->master_server_id != m_master->m_server_id)
            {
                is_suspect = true;
            }
            // or is disconnected but master host or port is wrong.
            else if (slave_status->slave_io_running == SlaveStatus::SLAVE_IO_CONNECTING &&
                     slave_status->slave_sql_running &&
                     (slave_status->master_host != m_master->m_server_base->server->address ||
                      slave_status->master_port != m_master->m_server_base->server->port))
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
 * intermediate step fails, the cluster may be left without a master.
 *
 * @param current_master Handle to current master server. If null, the autoselected server is written here.
 * @param new_master Handle to slave which should be promoted. If null, the autoselected server is written
 * here.
 * @param err_out json object for error printing. Can be NULL.
 * @return True if successful. If false, the cluster can be in various situations depending on which step
 * failed. In practice, manual intervention is usually required on failure.
 */
bool MariaDBMonitor::do_switchover(MariaDBServer** current_master, MariaDBServer** new_master,
                                   json_t** err_out)
{
    ss_dassert(current_master && new_master);
    MariaDBServer* demotion_target = NULL;
    if (*current_master == NULL)
    {
        // Autoselect current master.
        if (m_master && m_master->is_master())
        {
            demotion_target = m_master;
            *current_master = demotion_target;
        }
        else
        {
            PRINT_MXS_JSON_ERROR(err_out, "Could not autoselect current master for switchover. Cluster does "
                                 "not have a master or master is in maintenance.");
            return false;
        }
    }
    else
    {
        // No need to check a given current master, it has already been checked.
        demotion_target = *current_master;
    }

    if (m_master_gtid_domain < 0)
    {
        PRINT_MXS_JSON_ERROR(err_out, "Cluster gtid domain is unknown. Cannot switchover.");
        return false;
    }

    // Total time limit on how long this operation may take. Checked and modified after significant steps are
    // completed.
    int seconds_remaining = m_switchover_timeout;
    time_t start_time = time(NULL);

    // Step 1: Save all slaves except promotion target to an array. If we have a
    // user-defined master candidate, check it. Otherwise, autoselect.
    MariaDBServer* promotion_target = NULL;
    ServerArray redirectable_slaves;
    if (*new_master == NULL)
    {
        // Autoselect new master.
        promotion_target = select_new_master(&redirectable_slaves, err_out);
        if (promotion_target)
        {
            *new_master = promotion_target;
        }
        else
        {
            PRINT_MXS_JSON_ERROR(err_out, "Could not autoselect new master for switchover.");
            return false;
        }
    }
    else
    {
        // Check user-given new master. Some checks have already been performed but more is needed.
        if (switchover_check_preferred_master(*new_master, err_out))
        {
            promotion_target = *new_master;
            /* User-given candidate is good. Update info on all slave servers.
             * The update_slave_info()-call is not strictly necessary here, but it should be ran to keep this
             * path analogous with failover_select_new_master(). The later functions can then assume that
             * slave server info is up to date. If the master is replicating from external master, it is
             * updated by update_slave_info() but not added to array. */
            for (auto iter = m_servers.begin(); iter != m_servers.end(); iter++)
            {
                MariaDBServer* server = *iter;
                if (server != promotion_target && server->update_slave_info() && server != demotion_target)
                {
                    redirectable_slaves.push_back(server);
                }
            }
        }
        else
        {
            return false;
        }
    }
    ss_dassert(demotion_target && promotion_target);

    bool rval = false;
    // Step 2: Set read-only to on, flush logs, update master gtid:s
    if (switchover_demote_master(demotion_target, err_out))
    {
        bool catchup_and_promote_success = false;
        time_t step2_time = time(NULL);
        seconds_remaining -= difftime(step2_time, start_time);

        // Step 3: Wait for the slaves (including promotion target) to catch up with master.
        ServerArray catchup_slaves = redirectable_slaves;
        catchup_slaves.push_back(promotion_target);
        if (switchover_wait_slaves_catchup(catchup_slaves, demotion_target->m_gtid_binlog_pos,
                                           seconds_remaining, err_out))
        {
            time_t step3_time = time(NULL);
            int seconds_step3 = difftime(step3_time, step2_time);
            MXS_DEBUG("Switchover: slave catchup took %d seconds.", seconds_step3);
            seconds_remaining -= seconds_step3;

            // Step 4: On new master STOP and RESET SLAVE, set read-only to off.
            if (promote_new_master(promotion_target, err_out))
            {
                catchup_and_promote_success = true;
                // Step 5: Redirect slaves and start replication on old master.
                ServerArray redirected_slaves;
                bool start_ok = switchover_start_slave(demotion_target, promotion_target);
                if (start_ok)
                {
                    redirected_slaves.push_back(demotion_target);
                }
                int redirects = redirect_slaves(promotion_target, redirectable_slaves, &redirected_slaves);

                bool success = redirectable_slaves.empty() ? start_ok : start_ok || redirects > 0;
                if (success)
                {
                    time_t step5_time = time(NULL);
                    seconds_remaining -= difftime(step5_time, step3_time);

                    // Step 6: Finally, add an event to the new master to advance gtid and wait for the slaves
                    // to receive it. If using external replication, skip this step. Come up with an
                    // alternative later.
                    if (m_external_master_port != PORT_UNKNOWN)
                    {
                        MXS_WARNING("Replicating from external master, skipping final check.");
                        rval = true;
                    }
                    else if (wait_cluster_stabilization(promotion_target, redirected_slaves,
                                                        seconds_remaining))
                    {
                        rval = true;
                        time_t step6_time = time(NULL);
                        int seconds_step6 = difftime(step6_time, step5_time);
                        seconds_remaining -= seconds_step6;
                        MXS_DEBUG("Switchover: slave replication confirmation took %d seconds with "
                                  "%d seconds to spare.", seconds_step6, seconds_remaining);
                    }
                }
                else
                {
                    print_redirect_errors(demotion_target, redirectable_slaves, err_out);
                }
            }
        }

        if (!catchup_and_promote_success)
        {
            // Step 3 or 4 failed, try to undo step 2.
            const char QUERY_UNDO[] = "SET GLOBAL read_only=0;";
            if (mxs_mysql_query(demotion_target->m_server_base->con, QUERY_UNDO) == 0)
            {
                PRINT_MXS_JSON_ERROR(err_out, "read_only disabled on server %s.", demotion_target->name());
            }
            else
            {
                PRINT_MXS_JSON_ERROR(err_out, "Could not disable read_only on server %s: '%s'.",
                                     demotion_target->name(),
                                     mysql_error(demotion_target->m_server_base->con));
            }

            // Try to reactivate external replication if any.
            if (m_external_master_port != PORT_UNKNOWN)
            {
                start_external_replication(promotion_target, err_out);
            }
        }
    }
    return rval;
}

/**
 * Performs failover for a simple topology (1 master, N slaves, no intermediate masters).
 *
 * @param err_out Json output
 * @return True if successful
 */
bool MariaDBMonitor::do_failover(json_t** err_out)
{
    // Topology has already been tested to be simple.
    if (m_master_gtid_domain < 0)
    {
        PRINT_MXS_JSON_ERROR(err_out, "Cluster gtid domain is unknown. Cannot failover.");
        return false;
    }
    // Total time limit on how long this operation may take. Checked and modified after significant steps are
    // completed.
    int seconds_remaining = m_failover_timeout;
    time_t start_time = time(NULL);
    // Step 1: Select new master. Also populate a vector with all slaves not the selected master.
    ServerArray redirectable_slaves;
    MariaDBServer* new_master = select_new_master(&redirectable_slaves, err_out);
    if (new_master == NULL)
    {
        return false;
    }
    time_t step1_time = time(NULL);
    seconds_remaining -= difftime(step1_time, start_time);

    bool rval = false;
    // Step 2: Wait until relay log consumed.
    if (new_master->failover_wait_relay_log(seconds_remaining, err_out))
    {
        time_t step2_time = time(NULL);
        int seconds_step2 = difftime(step2_time, step1_time);
        MXS_DEBUG("Failover: relay log processing took %d seconds.", seconds_step2);
        seconds_remaining -= seconds_step2;

        // Step 3: Stop and reset slave, set read-only to 0.
        if (promote_new_master(new_master, err_out))
        {
            // Step 4: Redirect slaves.
            ServerArray redirected_slaves;
            int redirects = redirect_slaves(new_master, redirectable_slaves, &redirected_slaves);
            bool success = redirectable_slaves.empty() ? true : redirects > 0;
            if (success)
            {
                time_t step4_time = time(NULL);
                seconds_remaining -= difftime(step4_time, step2_time);

                // Step 5: Finally, add an event to the new master to advance gtid and wait for the slaves
                // to receive it. seconds_remaining can be 0 or less at this point. Even in such a case
                // wait_cluster_stabilization() may succeed if replication is fast enough. If using external
                // replication, skip this step. Come up with an alternative later.
                if (m_external_master_port != PORT_UNKNOWN)
                {
                    MXS_WARNING("Replicating from external master, skipping final check.");
                    rval = true;
                }
                else if (redirected_slaves.empty())
                {
                    // No slaves to check. Assume success.
                    rval = true;
                    MXS_DEBUG("Failover: no slaves to redirect, skipping stabilization check.");
                }
                else if (wait_cluster_stabilization(new_master, redirected_slaves, seconds_remaining))
                {
                    rval = true;
                    time_t step5_time = time(NULL);
                    int seconds_step5 = difftime(step5_time, step4_time);
                    seconds_remaining -= seconds_step5;
                    MXS_DEBUG("Failover: slave replication confirmation took %d seconds with "
                              "%d seconds to spare.", seconds_step5, seconds_remaining);
                }
            }
            else
            {
                print_redirect_errors(NULL, redirectable_slaves, err_out);
            }
        }
    }

    return rval;
}

/**
 * Demotes the current master server, preparing it for replicating from another server. This step can take a
 * while if long writes are running on the server.
 *
 * @param current_master Server to demote
 * @param info Current master info. Will be written to. TODO: Remove need for this.
 * @param err_out json object for error printing. Can be NULL.
 * @return True if successful.
 */
bool MariaDBMonitor::switchover_demote_master(MariaDBServer* current_master, json_t** err_out)
{
    MXS_NOTICE("Demoting server '%s'.", current_master->name());
    bool success = false;
    bool query_error = false;
    MYSQL* conn = current_master->m_server_base->con;
    const char* query = ""; // The next query to execute. Used also for error printing.
    // The presence of an external master changes several things.
    const bool external_master = SERVER_IS_SLAVE_OF_EXTERNAL_MASTER(current_master->m_server_base->server);

    if (external_master)
    {
        // First need to stop slave. read_only is probably on already, although not certain.
        query = "STOP SLAVE;";
        query_error = (mxs_mysql_query(conn, query) != 0);
        if (!query_error)
        {
            query = "RESET SLAVE ALL;";
            query_error = (mxs_mysql_query(conn, query) != 0);
        }
    }

    bool error_fetched = false;
    string error_desc;
    if (!query_error)
    {
        query = "SET GLOBAL read_only=1;";
        query_error = (mxs_mysql_query(conn, query) != 0);
        if (!query_error)
        {
            // If have external master, no writes are allowed so skip this step. It's not essential, just
            // adds one to gtid.
            if (!external_master)
            {
                query = "FLUSH TABLES;";
                query_error = (mxs_mysql_query(conn, query) != 0);
            }

            if (!query_error)
            {
                query = "FLUSH LOGS;";
                query_error = (mxs_mysql_query(conn, query) != 0);
                if (!query_error)
                {
                    query = "";
                    if (current_master->update_gtids())
                    {
                        success = true;
                    }
                }
            }

            if (!success)
            {
                // Somehow, a step after "SET read_only" failed. Try to set read_only back to 0. It may not
                // work since the connection is likely broken.
                error_desc = mysql_error(conn); // Read connection error before next step.
                error_fetched = true;
                mxs_mysql_query(conn, "SET GLOBAL read_only=0;");
            }
        }
    }

    if (query_error && !error_fetched)
    {
        error_desc = mysql_error(conn);
    }

    if (!success)
    {
        if (query_error)
        {
            if (error_desc.empty())
            {
                const char UNKNOWN_ERROR[] = "Demotion failed due to an unknown error when executing "
                                             "a query. Query: '%s'.";
                PRINT_MXS_JSON_ERROR(err_out, UNKNOWN_ERROR, query);
            }
            else
            {
                const char KNOWN_ERROR[] = "Demotion failed due to a query error: '%s'. Query: '%s'.";
                PRINT_MXS_JSON_ERROR(err_out, KNOWN_ERROR, error_desc.c_str(), query);
            }
        }
        else
        {
            const char * const GTID_ERROR = "Demotion failed due to an error in updating gtid:s. "
                                            "Check log for more details.";
            PRINT_MXS_JSON_ERROR(err_out, GTID_ERROR);
        }
    }
    else if (!m_demote_sql_file.empty() && !current_master->run_sql_from_file(m_demote_sql_file, err_out))
    {
        PRINT_MXS_JSON_ERROR(err_out, "%s execution failed when demoting server '%s'.",
                             CN_DEMOTION_SQL_FILE, current_master->name());
        success = false;
    }

    return success;
}

/**
 * Wait until slave replication catches up with the master gtid for all slaves in the vector.
 *
 * @param slave Slaves to wait on
 * @param gtid Which gtid must be reached
 * @param total_timeout Maximum wait time in seconds
 * @param err_out json object for error printing. Can be NULL.
 * @return True, if target gtid was reached within allotted time for all servers
 */
bool MariaDBMonitor::switchover_wait_slaves_catchup(const ServerArray& slaves, const GtidList& gtid,
                                                    int total_timeout, json_t** err_out)
{
    bool success = true;
    int seconds_remaining = total_timeout;

    for (auto iter = slaves.begin(); iter != slaves.end() && success; iter++)
    {
        if (seconds_remaining <= 0)
        {
            success = false;
        }
        else
        {
            time_t begin = time(NULL);
            MariaDBServer* slave_server = *iter;
            if (slave_server->wait_until_gtid(gtid, seconds_remaining, err_out))
            {
                seconds_remaining -= difftime(time(NULL), begin);
            }
            else
            {
                success = false;
            }
        }
    }
    return success;
}

/**
 * Send an event to new master and wait for slaves to get the event.
 *
 * @param new_master Where to send the event
 * @param slaves Servers to monitor
 * @param seconds_remaining How long can we wait
 * @return True, if at least one slave got the new event within the time limit
 */
bool MariaDBMonitor::wait_cluster_stabilization(MariaDBServer* new_master, const ServerArray& slaves,
                                                int seconds_remaining)
{
    ss_dassert(!slaves.empty());
    bool rval = false;
    time_t begin = time(NULL);

    if (mxs_mysql_query(new_master->m_server_base->con, "FLUSH TABLES;") == 0 &&
        new_master->update_gtids())
    {
        int query_fails = 0;
        int repl_fails = 0;
        int successes = 0;
        const GtidList& target = new_master->m_gtid_current_pos;
        ServerArray wait_list = slaves; // Check all the servers in the list
        bool first_round = true;
        bool time_is_up = false;

        while (!wait_list.empty() && !time_is_up)
        {
            if (!first_round)
            {
                thread_millisleep(500);
            }

            // Erasing elements from an array, so iterate from last to first
            int i = wait_list.size() - 1;
            while (i >= 0)
            {
                MariaDBServer* slave = wait_list[i];
                if (slave->update_gtids() && slave->do_show_slave_status() && !slave->m_slave_status.empty())
                {
                    if (!slave->m_slave_status[0].last_error.empty())
                    {
                        // IO or SQL error on slave, replication is a fail
                        MXS_WARNING("Slave '%s' cannot start replication: '%s'.", slave->name(),
                                    slave->m_slave_status[0].last_error.c_str());
                        wait_list.erase(wait_list.begin() + i);
                        repl_fails++;
                    }
                    else if (GtidList::events_ahead(target, slave->m_gtid_current_pos,
                                                    GtidList::MISSING_DOMAIN_IGNORE) == 0)
                    {
                        // This slave has reached the same gtid as master, remove from list
                        wait_list.erase(wait_list.begin() + i);
                        successes++;
                    }
                }
                else
                {
                    wait_list.erase(wait_list.begin() + i);
                    query_fails++;
                }
                i--;
            }

            first_round = false; // Sleep at start of next iteration
            if (difftime(time(NULL), begin) >= seconds_remaining)
            {
                time_is_up = true;
            }
        }

        auto fails = repl_fails + query_fails + wait_list.size();
        if (fails > 0)
        {
            const char MSG[] = "Replication from the new master could not be confirmed for %lu slaves. "
                               "%d encountered an I/O or SQL error, %d failed to reply and %lu did not "
                               "advance in Gtid until time ran out.";
            MXS_WARNING(MSG, fails, repl_fails, query_fails, wait_list.size());
        }
        rval = (successes > 0);
    }
    else
    {
        MXS_ERROR("Could not confirm replication after switchover/failover because query to "
                  "the new master failed.");
    }
    return rval;
}

/**
 * Check that the given slave is a valid promotion candidate.
 *
 * @param preferred Preferred new master
 * @param err_out Json object for error printing. Can be NULL.
 * @return True, if given slave is a valid promotion candidate.
 */
bool MariaDBMonitor::switchover_check_preferred_master(MariaDBServer* preferred, json_t** err_out)
{
    ss_dassert(preferred);
    bool rval = true;
    if (!preferred->update_slave_info() || !preferred->check_replication_settings())
    {
        PRINT_MXS_JSON_ERROR(err_out, "The requested server '%s' is not a valid promotion candidate.",
                             preferred->name());
        rval = false;
    }
    return rval;
}

/**
 * Prepares a server for the replication master role.
 *
 * @param new_master The new master server
 * @param err_out json object for error printing. Can be NULL.
 * @return True if successful
 */
bool MariaDBMonitor::promote_new_master(MariaDBServer* new_master, json_t** err_out)
{
    bool success = false;
    MYSQL* new_master_conn = new_master->m_server_base->con;
    MXS_NOTICE("Promoting server '%s' to master.", new_master->name());
    const char* query = "STOP SLAVE;";
    if (mxs_mysql_query(new_master_conn, query) == 0)
    {
        query = "RESET SLAVE ALL;";
        if (mxs_mysql_query(new_master_conn, query) == 0)
        {
            query = "SET GLOBAL read_only=0;";
            if (mxs_mysql_query(new_master_conn, query) == 0)
            {
                success = true;
            }
        }
    }

    if (!success)
    {
        PRINT_MXS_JSON_ERROR(err_out, "Promotion failed: '%s'. Query: '%s'.",
                             mysql_error(new_master_conn), query);
    }
    else
    {
        // Promotion commands ran successfully, run promotion sql script file before external replication.
        if (!m_promote_sql_file.empty() && !new_master->run_sql_from_file(m_promote_sql_file, err_out))
        {
            PRINT_MXS_JSON_ERROR(err_out, "%s execution failed when promoting server '%s'.",
                                 CN_PROMOTION_SQL_FILE, new_master->name());
            success = false;
        }
        // If the previous master was a slave to an external master, start the equivalent slave connection on
        // the new master. Success of replication is not checked.
        else if (m_external_master_port != PORT_UNKNOWN && !start_external_replication(new_master, err_out))
        {
            success = false;
        }
    }

    return success;
}

/**
 * Select a new master. Also add slaves which should be redirected to an array.
 *
 * @param out_slaves Vector for storing slave servers.
 * @param err_out json object for error printing. Can be NULL.
 * @return The found master, or NULL if not found
 */
MariaDBServer* MariaDBMonitor::select_new_master(ServerArray* slaves_out, json_t** err_out)
{
    ss_dassert(slaves_out && slaves_out->size() == 0);
    /* Select a new master candidate. Selects the one with the latest event in relay log.
     * If multiple slaves have same number of events, select the one with most processed events. */
    MariaDBServer* current_best = NULL;
    // Servers that cannot be selected because of exclusion, but seem otherwise ok.
    ServerArray valid_but_excluded;
    // Index of the current best candidate in slaves_out
    int master_vector_index = -1;

    for (auto iter = m_servers.begin(); iter != m_servers.end(); iter++)
    {
        /* If a server cannot be connected to, it won't be considered for promotion or redirected.
         * Do not worry about the exclusion list yet, querying the excluded servers is ok.
         * If master is replicating from external master, it is updated by update_slave_info()
         * but not added to array. */
        MariaDBServer* cand = *iter;
        if (cand->update_slave_info() && cand != m_master)
        {
            slaves_out->push_back(cand);
            // Check that server is not in the exclusion list while still being a valid choice.
            if (server_is_excluded(cand) && cand->check_replication_settings(WARNINGS_OFF))
            {
                valid_but_excluded.push_back(cand);
                const char CANNOT_SELECT[] = "Promotion candidate '%s' is excluded from new "
                                             "master selection.";
                MXS_INFO(CANNOT_SELECT, cand->name());
            }
            else if (cand->check_replication_settings())
            {
                // If no new master yet, accept any valid candidate. Otherwise check.
                if (current_best == NULL || is_candidate_better(current_best, cand, m_master_gtid_domain))
                {
                    // The server has been selected for promotion, for now.
                    current_best = cand;
                    master_vector_index = slaves_out->size() - 1;
                }
            }
        }
    }

    if (current_best)
    {
        // Remove the selected master from the vector.
        auto it_remove = slaves_out->begin();
        it_remove += master_vector_index;
        slaves_out->erase(it_remove);
    }

    // Check if any of the excluded servers would be better than the best candidate.
    for (auto iter = valid_but_excluded.begin(); iter != valid_but_excluded.end(); iter++)
    {
        MariaDBServer* excluded_info = *iter;
        const char* excluded_name = (*iter)->name();
        if (current_best == NULL)
        {
            const char EXCLUDED_ONLY_CAND[] = "Server '%s' is a viable choice for new master, "
                                              "but cannot be selected as it's excluded.";
            MXS_WARNING(EXCLUDED_ONLY_CAND, excluded_name);
            break;
        }
        else if (is_candidate_better(current_best, excluded_info, m_master_gtid_domain))
        {
            // Print a warning if this server is actually a better candidate than the previous best.
            const char EXCLUDED_CAND[] = "Server '%s' is superior to current best candidate '%s', "
                                         "but cannot be selected as it's excluded. This may lead to "
                                         "loss of data if '%s' is ahead of other servers.";
            MXS_WARNING(EXCLUDED_CAND, excluded_name, current_best->name(), excluded_name);
            break;
        }
    }

    if (current_best == NULL)
    {
        PRINT_MXS_JSON_ERROR(err_out, "No suitable promotion candidate found.");
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
    for (auto iter = m_excluded_servers.begin(); iter != m_excluded_servers.end(); iter++)
    {
        if (*iter == server)
        {
            return true;
        }
    }
    return false;
}

/**
 * Is the candidate a better choice for master than the previous best?
 *
 * @param current_best_info Server info of current best choice
 * @param candidate_info Server info of new candidate
 * @param gtid_domain Which domain to compare
 * @return True if candidate is better
 */
bool MariaDBMonitor::is_candidate_better(const MariaDBServer* current_best, const MariaDBServer* candidate,
                                         uint32_t gtid_domain)
{
    uint64_t cand_io = candidate->m_slave_status[0].gtid_io_pos.get_gtid(gtid_domain).m_sequence;
    uint64_t cand_processed = candidate->m_gtid_current_pos.get_gtid(gtid_domain).m_sequence;
    uint64_t curr_io = current_best->m_slave_status[0].gtid_io_pos.get_gtid(gtid_domain).m_sequence;
    uint64_t curr_processed = current_best->m_gtid_current_pos.get_gtid(gtid_domain).m_sequence;

    bool cand_updates = candidate->m_rpl_settings.log_slave_updates;
    bool curr_updates = current_best->m_rpl_settings.log_slave_updates;
    bool is_better = false;
    // Accept a slave with a later event in relay log.
    if (cand_io > curr_io)
    {
        is_better = true;
    }
    // If io sequences are identical, the slave with more events processed wins.
    else if (cand_io == curr_io)
    {
        if (cand_processed > curr_processed)
        {
            is_better = true;
        }
        // Finally, if binlog positions are identical, prefer a slave with log_slave_updates.
        else if (cand_processed == curr_processed && cand_updates && !curr_updates)
        {
            is_better = true;
        }
    }
    return is_better;
}

/**
 * Check that the given server is a master and it's the only master.
 *
 * @param suggested_curr_master     The server to check, given by user.
 * @param error_out                 On output, error object if function failed.
 * @return True if current master seems ok. False, if there is some error with the
 * specified current master.
 */
bool MariaDBMonitor::switchover_check_current(const MXS_MONITORED_SERVER* suggested_curr_master,
                                              json_t** error_out) const
{
    ss_dassert(suggested_curr_master);
    bool server_is_master = false;
    MXS_MONITORED_SERVER* extra_master = NULL; // A master server which is not the suggested one
    for (MXS_MONITORED_SERVER* mon_serv = m_monitor_base->monitored_servers;
         mon_serv != NULL && extra_master == NULL;
         mon_serv = mon_serv->next)
    {
        if (SERVER_IS_MASTER(mon_serv->server))
        {
            if (mon_serv == suggested_curr_master)
            {
                server_is_master = true;
            }
            else
            {
                extra_master = mon_serv;
            }
        }
    }

    if (!server_is_master)
    {
        PRINT_MXS_JSON_ERROR(error_out, "Server '%s' is not the current master or it's in maintenance.",
                             suggested_curr_master->server->name);
    }
    else if (extra_master)
    {
        PRINT_MXS_JSON_ERROR(error_out, "Cluster has an additional master server '%s'.",
                             extra_master->server->name);
    }
    return server_is_master && !extra_master;
}

/**
 * Check whether specified new master is acceptable.
 *
 * @param monitored_server      The server to check against.
 * @param error                 On output, error object if function failed.
 *
 * @return True, if suggested new master is a viable promotion candidate.
 */
bool MariaDBMonitor::switchover_check_new(const MXS_MONITORED_SERVER* monitored_server, json_t** error)
{
    SERVER* server = monitored_server->server;
    const char* name = server->name;
    bool is_master = SERVER_IS_MASTER(server);
    bool is_slave = SERVER_IS_SLAVE(server);

    if (is_master)
    {
        const char IS_MASTER[] = "Specified new master '%s' is already the current master.";
        PRINT_MXS_JSON_ERROR(error, IS_MASTER, name);
    }
    else if (!is_slave)
    {
        const char NOT_SLAVE[] = "Specified new master '%s' is not a slave.";
        PRINT_MXS_JSON_ERROR(error, NOT_SLAVE, name);
    }

    return !is_master && is_slave;
}

/**
 * Check that preconditions for a failover are met.
 *
 * @param error_out JSON error out
 * @return True if failover may proceed
 */
bool MariaDBMonitor::failover_check(json_t** error_out)
{
    // Check that there is no running master and that there is at least one running server in the cluster.
    // Also, all slaves must be using gtid-replication.
    int slaves = 0;
    bool error = false;

    for (auto iter = m_servers.begin(); iter != m_servers.end(); iter++)
    {
        MariaDBServer* server = *iter;
        uint64_t status_bits = server->m_server_base->server->status;
        uint64_t master_up = (SERVER_MASTER | SERVER_RUNNING);
        if ((status_bits & master_up) == master_up)
        {
            string master_up_msg = string("Master server '") + server->name() + "' is running";
            if (status_bits & SERVER_MAINT)
            {
                master_up_msg += ", although in maintenance mode";
            }
            master_up_msg += ".";
            PRINT_MXS_JSON_ERROR(error_out, "%s", master_up_msg.c_str());
            error = true;
        }
        else if (server->is_slave())
        {
            if (server->uses_gtid(error_out))
            {
                slaves++;
            }
            else
            {
                error = true;
            }
        }
    }

    if (error)
    {
        PRINT_MXS_JSON_ERROR(error_out, "Failover not allowed due to errors.");
    }
    else if (slaves == 0)
    {
        PRINT_MXS_JSON_ERROR(error_out, "No running slaves, cannot failover.");
    }
    return !error && slaves > 0;
}

/**
 * @brief Process possible failover event
 *
 * If a master failure has occurred and MaxScale is configured with failover functionality, this fuction
 * executes failover to select and promote a new master server. This function should be called immediately
 * after @c mon_process_state_changes. If an error occurs, this method disables automatic failover.
 *
 * @return True if failover was performed, or at least attempted
*/
bool MariaDBMonitor::handle_auto_failover()
{
    const char RE_ENABLE_FMT[] = "%s To re-enable failover, manually set '%s' to 'true' for monitor "
                                 "'%s' via MaxAdmin or the REST API, or restart MaxScale.";
    bool cluster_modified = false;
    if (config_get_global_options()->passive || (m_master && m_master->is_master()))
    {
        return cluster_modified;
    }

    if (failover_not_possible())
    {
        const char PROBLEMS[] = "Failover is not possible due to one or more problems in the "
                                "replication configuration, disabling automatic failover. Failover "
                                "should only be enabled after the replication configuration has been "
                                "fixed.";
        MXS_ERROR(RE_ENABLE_FMT, PROBLEMS, CN_AUTO_FAILOVER, m_monitor_base->name);
        m_auto_failover = false;
        disable_setting(CN_AUTO_FAILOVER);
        return cluster_modified;
    }

    // If master seems to be down, check if slaves are receiving events.
    if (m_verify_master_failure && m_master && m_master->is_down() && slave_receiving_events())
    {
        MXS_INFO("Master failure not yet confirmed by slaves, delaying failover.");
        return cluster_modified;
    }

    MariaDBServer* failed_master = NULL;
    for (auto iter = m_servers.begin(); iter != m_servers.end(); iter++)
    {
        MariaDBServer* server = *iter;
        MXS_MONITORED_SERVER* mon_server = server->m_server_base;
        if (mon_server->new_event && mon_server->server->last_event == MASTER_DOWN_EVENT)
        {
            if (mon_server->server->active_event)
            {
                // MaxScale was active when the event took place
                failed_master = server;
            }
            else
            {
                /* If a master_down event was triggered when this MaxScale was passive, we need to execute
                 * the failover script again if no new masters have appeared. */
                int64_t timeout = MXS_SEC_TO_CLOCK(m_failover_timeout);
                int64_t t = mxs_clock() - mon_server->server->triggered_at;

                if (t > timeout)
                {
                    MXS_WARNING("Failover of server '%s' did not take place within %u seconds, "
                                "failover needs to be re-triggered", server->name(), m_failover_timeout);
                    failed_master = server;
                }
            }
        }
    }

    if (failed_master)
    {
        if (m_failcount > 1 && failed_master->m_server_base->mon_err_count == 1)
        {
            MXS_WARNING("Master has failed. If master status does not change in %d monitor passes, failover "
                        "begins.", m_failcount - 1);
        }
        else if (failed_master->m_server_base->mon_err_count >= m_failcount)
        {
            MXS_NOTICE("Performing automatic failover to replace failed master '%s'.", failed_master->name());
            failed_master->m_server_base->new_event = false;
            if (failover_check(NULL))
            {
                if (!do_failover(NULL))
                {
                    const char FAILED[] = "Failed to perform failover, disabling automatic failover.";
                    MXS_ERROR(RE_ENABLE_FMT, FAILED, CN_AUTO_FAILOVER, m_monitor_base->name);
                    m_auto_failover = false;
                    disable_setting(CN_AUTO_FAILOVER);
                }
                cluster_modified = true;
            }
        }
    }

    return cluster_modified;
}

bool MariaDBMonitor::failover_not_possible()
{
    bool rval = false;

    for (MXS_MONITORED_SERVER* s = m_monitor_base->monitored_servers; s; s = s->next)
    {
        MariaDBServer* info = get_server_info(s);

        if (info->m_slave_status.size() > 1)
        {
            MXS_ERROR("Server '%s' is configured to replicate from multiple "
                      "masters, failover is not possible.", s->server->name);
            rval = true;
        }
    }

    return rval;
}

/**
 * Check if a slave is receiving events from master.
 *
 * @return True, if a slave has an event more recent than master_failure_timeout.
 */
bool MariaDBMonitor::slave_receiving_events()
{
    ss_dassert(m_master);
    bool received_event = false;
    int64_t master_id = m_master->m_server_base->server->node_id;
    for (MXS_MONITORED_SERVER* server = m_monitor_base->monitored_servers; server; server = server->next)
    {
        MariaDBServer* info = get_server_info(server);

        if (!info->m_slave_status.empty() &&
            info->m_slave_status[0].slave_io_running == SlaveStatus::SLAVE_IO_YES &&
            info->m_slave_status[0].master_server_id == master_id &&
            difftime(time(NULL), info->m_latest_event) < m_master_failure_timeout)
        {
            /**
             * The slave is still connected to the correct master and has received events. This means that
             * while MaxScale can't connect to the master, it's probably still alive.
             */
            received_event = true;
            break;
        }
    }
    return received_event;
}

/**
 * Print a redirect error to logs. If err_out exists, generate a combined error message by querying all
 * the server parameters for connection errors and append these errors to err_out.
 *
 * @param demotion_target If not NULL, this is the first server to query.
 * @param redirectable_slaves Other servers to query for errors.
 * @param err_out If not null, the error output object.
 */
static void print_redirect_errors(MariaDBServer* first_server, const ServerArray& servers,
                                  json_t** err_out)
{
    // Individual server errors have already been printed to the log.
    // For JSON, gather the errors again.
    const char* const MSG = "Could not redirect any slaves to the new master.";
    MXS_ERROR(MSG);
    if (err_out)
    {
        ServerArray failed_slaves;
        if (first_server)
        {
            failed_slaves.push_back(first_server);
        }
        for (auto iter = servers.begin(); iter != servers.end(); iter++)
        {
            failed_slaves.push_back(*iter);
        }

        string combined_error = get_connection_errors(failed_slaves);
        *err_out = mxs_json_error_append(*err_out, "%s Errors: %s.", MSG, combined_error.c_str());
    }
}

/**
 * Check cluster and parameters for suitability to switchover. Also writes found servers to output pointers.
 * If a server parameter is NULL, the corresponding output parameter is not written to.
 *
 * @param new_master New master requested by the user. Can be null for autoselect.
 * @param current_master Current master given by the user. Can be null for autoselect.
 * @param new_master_out Where to write found new master.
 * @param current_master_out Where to write found current master.
 * @param error_out Error output, can be null.
 * @return True if cluster is suitable and server parameters were valid and found.
 */
bool MariaDBMonitor::switchover_check(SERVER* new_master, SERVER* current_master,
                                      MariaDBServer** new_master_out, MariaDBServer** current_master_out,
                                      json_t** error_out)
{
    bool new_master_ok = true, current_master_ok = true;
    const char NO_SERVER[] = "Server '%s' is not a member of monitor '%s'.";
    // Check that both servers are ok if specified. Null is a valid value.
    if (new_master)
    {
        auto mon_new_master = mon_get_monitored_server(m_monitor_base, new_master);
        if (mon_new_master == NULL)
        {
            new_master_ok = false;
            PRINT_MXS_JSON_ERROR(error_out, NO_SERVER, new_master->name, m_monitor_base->name);
        }
        else if (!switchover_check_new(mon_new_master, error_out))
        {
            new_master_ok = false;
        }
        else
        {
            *new_master_out = get_server_info(mon_new_master);
        }
    }

    if (current_master)
    {
        auto mon_curr_master = mon_get_monitored_server(m_monitor_base, current_master);
        if (mon_curr_master == NULL)
        {
            current_master_ok = false;
            PRINT_MXS_JSON_ERROR(error_out, NO_SERVER, current_master->name, m_monitor_base->name);
        }
        else if (!switchover_check_current(mon_curr_master, error_out))
        {
            current_master_ok = false;
        }
        else
        {
            *current_master_out = get_server_info(mon_curr_master);
        }
    }

    // Check that all slaves are using gtid-replication.
    bool gtid_ok = true;
    for (auto iter = m_servers.begin(); iter != m_servers.end(); iter++)
    {
        if ((*iter)->is_slave() && !(*iter)->uses_gtid(error_out))
        {
            gtid_ok = false;
        }
    }

    return new_master_ok && current_master_ok && gtid_ok;
}
