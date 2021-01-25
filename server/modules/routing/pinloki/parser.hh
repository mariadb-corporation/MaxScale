/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-01-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <string>
#include <vector>
#include <map>

namespace pinloki
{

// CHANGE MASTER TO values that are parsed
enum class ChangeMasterType
{
    MASTER_HOST,
    MASTER_PORT,
    MASTER_USER,
    MASTER_PASSWORD,
    MASTER_USE_GTID,
    MASTER_SSL,
    MASTER_SSL_CA,
    MASTER_SSL_CAPATH,
    MASTER_SSL_CERT,
    MASTER_SSL_CRL,
    MASTER_SSL_CRLPATH,
    MASTER_SSL_KEY,
    MASTER_SSL_CIPHER,
    MASTER_SSL_VERIFY_SERVER_CERT,
    // These are errors in Pinloki::change_master()
    MASTER_LOG_FILE,
    MASTER_LOG_POS,
    RELAY_LOG_FILE,
    RELAY_LOG_POS,
    // This one is ignored, logs a warning
    MASTER_HEARTBEAT_PERIOD,
    // These are also errors, but with a "not supported yet" msg.
    MASTER_BIND,
    MASTER_CONNECT_RETRY,
    MASTER_DELAY,
    IGNORE_SERVER_IDS,
    DO_DOMAIN_IDS,
    IGNORE_DOMAIN_IDS,
    END
};

std::string to_string(ChangeMasterType type);

namespace parser
{

using ChangeMasterValues = std::map<ChangeMasterType, std::string>;

struct Handler
{
    virtual void select(const std::vector<std::string>& values) = 0;
    virtual void set(const std::string& key, const std::string& value) = 0;

    virtual void change_master_to(const ChangeMasterValues& values) = 0;
    virtual void start_slave() = 0;
    virtual void stop_slave() = 0;
    virtual void reset_slave() = 0;
    virtual void show_slave_status(bool all) = 0;
    virtual void show_master_status() = 0;
    virtual void show_binlogs() = 0;
    virtual void show_variables(const std::string& like) = 0;
    virtual void master_gtid_wait(const std::string& gtid, int timeout) = 0;

    virtual void purge_logs(const std::string& up_to) = 0;

    virtual void error(const std::string& err) = 0;
};

void parse(const std::string& line, Handler* handler);
}
}
