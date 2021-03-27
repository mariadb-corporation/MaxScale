/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <maxtest/mariadb_nodes.hh>

namespace maxtest
{
/**
 * Standard MariaDB master-slave replication cluster.
 */
class ReplicationCluster : public MariaDBCluster
{
public:
    ReplicationCluster(SharedData* shared);

    int                start_replication() override;
    bool               check_replication() override;
    void               sync_slaves(int node = 0) override;
    const std::string& type_string() const override;

    /**
     * @brief find_master Tries to find Master node
     * @return Index of Master node
     */
    int find_master();

    /**
     * @brief change_master set a new master node for Master/Slave setup
     * @param NewMaster index of new Master node
     * @param OldMaster index of current Master node
     */
    void change_master(int NewMaster, int OldMaster);

    /**
     * @brief Creates 'repl' user on all nodes
     * @return 0 if everything is ok
     */
    int set_repl_user();

    /**
     * @brief executes 'CHANGE MASTER TO ..' and 'START SLAVE'
     * @param MYSQL conn struct of slave node
     * @param master_host IP address of master node
     * @param master_port port of master node
     * @param log_file name of log file
     * @param log_pos initial position
     * @return 0 if everything is ok
     */
    int set_slave(MYSQL* conn, const char* master_host, int master_port,
                  const char* log_file, const char* log_pos);

    /**
     * Configure a server as a slave of another server
     *
     * The servers are configured with GTID replicating using the configured
     * GTID position, either slave_pos or current_pos.
     *
     * @param slave  The node index to assign as slave
     * @param master The node index of the master
     * @param type   Replication type
     */
    void replicate_from(int slave, int master, const char* type = "current_pos");

    // Replicates from a host and a port instead of a known server
    void replicate_from(int slave, const std::string& host, uint16_t port, const char* type = "current_pos");

private:
    bool check_master_node(MYSQL* conn);
    bool bad_slave_thread_status(MYSQL* conn, const char* field, int node);
    bool wrong_replication_type(MYSQL* conn);
};
}
