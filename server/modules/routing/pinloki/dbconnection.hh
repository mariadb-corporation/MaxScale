/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <maxsql/ccdefs.hh>
#include <maxbase/host.hh>
#include <maxbase/exception.hh>
#include "resultset.hh"
#include "gtid.hh"
#include "maria_rpl_event.hh"

#include <mysql.h>

#include <chrono>

struct st_mysql;
struct st_mysql_res;

namespace maxsql
{

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
    };

    Connection(const ConnectionDetails& details);
    Connection(Connection&&) = delete;
    ~Connection();

    void          start_replication(unsigned int server_id, GtidList gtid = GtidList());
    MariaRplEvent get_rpl_msg();

    /**
     * @brief ping - ping the server, and return mariadb_error()
     * @return
     */
    uint64_t ping();

    /**
     * @brief begin_trx
     */
    void begin_trx();

    /**
     * @brief commit_trx
     */
    void commit_trx();

    /**
     * @brief rollback_trx
     */
    void rollback_trx();

    /**
     * @brief nesting_level - begin_trx(); begin_trx; => nesting_level()==2
     * @return
     */
    int nesting_level();

    /**
     * @brief query
     * @param sql
     */
    void query(const std::string& sql);

    /**
     * @brief affected_rows
     * @return
     */
    int affected_rows() const;

    /**
     * @brief result_set
     * @return
     */
    ResultSet result_set();

    /**
     * @brief discard_result
     */
    void discard_result();

    /**
     * @brief host
     * @return
     */
    maxbase::Host host() const;

    /**
     * @brief mariadb_error - will not return an error if the server
     *                        has timed out.
     * @return mysql_errno()
     */
    uint64_t mariadb_error();

    /**
     * @brief mariadb_error_str
     * @return error string, or empty() if there is no error
     */
    std::string mariadb_error_str();

    /** Run any Connector/C function. Don't run a function that is explicitely in
     *  this class's interface.
     *  Example: mc.call(mysql_select_db, "information_schema");
     */
    template<typename R, typename ... Args>
    R call(R (&foo)(st_mysql*, Args ...), Args ... args) const;
private:
    st_mysql*         m_conn {nullptr};
    st_mariadb_rpl*   m_rpl {nullptr};
    ConnectionDetails m_details;
    int               m_nesting_level = 0;

    void connect();
};

template<typename R, typename ... Args>
R Connection::call(R (&foo)(st_mysql*, Args ...), Args... args) const
{
    return foo(m_conn, std::forward<Args>(args)...);
}
}
