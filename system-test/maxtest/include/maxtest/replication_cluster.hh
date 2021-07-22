/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-19
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

    bool start_replication() override;

    /**
     * Wait until slaves are up-to-date. Only considers the first gtid-triplet.
     */
    bool               sync_slaves(int node = 0);
    const std::string& type_string() const override;

    /**
     * @brief change_master set a new master node for Master/Slave setup
     * @param NewMaster index of new Master node
     * @param OldMaster index of current Master node
     */
    void change_master(int NewMaster, int OldMaster);

    enum class GtidType {CURRENT_POS, SLAVE_POS};

    /**
     * Configure a server as a slave of another server
     *
     * @param slave  The node index to assign as slave
     * @param master The node index of the master
     */
    void replicate_from(int slave, int master);

    /*
     * Replicates from a host and a port instead of a known server */
    void replicate_from(int slave, const std::string& host, uint16_t port);

    void replicate_from(int slave, const std::string& host, uint16_t port,
                        GtidType type, const std::string& conn_name, bool reset);

    const std::string& nwconf_prefix() const override;
    const std::string& name() const override;

    virtual std::string get_srv_cnf_filename(int node) override;

    bool create_users(int i) override;

private:
    bool remove_all_slave_conns(MariaDBServer* server);
    bool good_slave_thread_status(MariaDBServer* slave, MariaDBServer* master);
    bool check_replication() override;

    std::string gen_change_master_cmd(MariaDBServer* master);
};
}
