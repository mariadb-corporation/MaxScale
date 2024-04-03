/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <maxsql/ccdefs.hh>
#include <maxbase/host.hh>
#include <maxbase/exception.hh>
#include "gtid.hh"
#include "maria_rpl_event.hh"

#include <mysql.h>

#include <chrono>

namespace maxsql
{
DEFINE_EXCEPTION(DatabaseError);

// https://mariadb.com/kb/en/gtid_event/#flags
enum GtidFlags
{
    F_STANDALONE      = 1,
    F_GROUP_COMMIT_ID = 2,
    F_TRANSACTIONAL   = 4,
    F_ALLOW_PARALLEL  = 8,
    F_WAITED          = 16,
    F_DDL             = 32,
};

class Connection
{
public:

    struct ConnectionDetails
    {
        maxbase::Host        host;
        std::string          database;  // may be empty
        std::string          user;
        std::string          password;
        unsigned long        flags = 0;
        std::chrono::seconds timeout = std::chrono::seconds(10);

        // TLS variables
        bool        ssl = false;
        std::string ssl_ca;
        std::string ssl_capath;
        std::string ssl_cert;
        std::string ssl_crl;
        std::string ssl_crlpath;
        std::string ssl_key;
        std::string ssl_cipher;
        bool        ssl_verify_server_cert = false;

        bool proxy_protocol = false;
    };

    Connection(const ConnectionDetails& details);
    Connection(Connection&&) = delete;
    ~Connection();

    void          start_replication(unsigned int server_id, bool semi_sync, const GtidList& gtid);
    MariaRplEvent get_rpl_msg();

    /**
     * @brief query
     * @param sql
     */
    void query(const std::string& sql);

    /**
     * @brief host
     * @return
     */
    maxbase::Host host() const;

    /**
     * @brief mariadb_error_str
     * @return error string, or empty() if there is no error
     */
    std::string mariadb_error_str();

private:
    MYSQL*            m_conn {nullptr};
    MARIADB_RPL*      m_rpl {nullptr};
    ConnectionDetails m_details;

    void connect();
};
}
