/*
 * Copyright (c) 2019 MariaDB Corporation Ab
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

#pragma once

#include <string>
#include <memory>

#include "config.hh"
#include "rpl.hh"

namespace cdc
{

// Final name pending
class Replicator
{
public:
    Replicator(const Replicator&) = delete;
    Replicator& operator=(const Replicator&) = delete;

    /**
     * Create a new data replicator
     *
     * @param cnf The configuration to use
     *
     * @return The new Replicator instance
     */
    static std::unique_ptr<Replicator> start(const Config& cnf, SRowEventHandler handler);

    /**
     * Check if the replicator is OK
     *
     * @return True if everything is OK. False if any errors have occurred and the replicator has stopped.
     */
    bool ok() const;

    /**
     * Request all files to be rotated
     *
     * The actual effect of the rotation depends on the RowEventHandler implementation.
     */
    void rotate();

    /**
     * Get current GTID position
     *
     * @return The current GTID position or an empty string if no position has been reached
     */
    std::string gtid_pos() const;

    /**
     * Destroys the Replicator and stops the processing of data
     */
    ~Replicator();

    /**
     * Get the server from which the replication is being done from
     */
    SERVER* target() const;

private:
    class Imp;
    Replicator(const Config& cnf, SRowEventHandler handler);

    // Pointer to the implementation of the abstract interface
    std::unique_ptr<Replicator::Imp> m_imp;
};
}
