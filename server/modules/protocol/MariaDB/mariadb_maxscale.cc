/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */


/**
 * @file mysql_utils.c  - Binary MySQL data processing utilities
 *
 * This file contains functions that are used when processing binary format
 * information. The MySQL protocol uses the binary format in result sets and
 * row based replication.
 */

#include <maxscale/protocol/mariadb/maxscale.hh>

#include <string.h>
#include <strings.h>
#include <mysql.h>

#include <maxbase/format.hh>
#include <maxsql/mariadb.hh>
#include <maxsql/mariadb_connector.hh>
#include <maxscale/config.hh>
#include <maxscale/connection_metadata.hh>

MYSQL* mxs_mysql_real_connect(MYSQL* con, const char* address, int port,
                              const char* user, const char* passwd,
                              const mxb::SSLConfig& ssl, int flags)
{
    if (ssl.enabled)
    {
        char enforce_tls = 1;
        mysql_optionsv(con, MYSQL_OPT_SSL_ENFORCE, (void*)&enforce_tls);

        // If an option is empty, a null-pointer should be given to mysql_ssl_set.
        const char* ssl_key = ssl.key.empty() ? nullptr : ssl.key.c_str();
        const char* ssl_cert = ssl.cert.empty() ? nullptr : ssl.cert.c_str();
        const char* ssl_ca = ssl.ca.empty() ? nullptr : ssl.ca.c_str();
        mysql_ssl_set(con, ssl_key, ssl_cert, ssl_ca, NULL, NULL);

        switch (ssl.version)
        {
        case mxb::ssl_version::TLS11:
            mysql_optionsv(con, MARIADB_OPT_TLS_VERSION, "TLSv1.1,TLSv1.2,TLSv1.3");
            break;

        case mxb::ssl_version::TLS12:
            mysql_optionsv(con, MARIADB_OPT_TLS_VERSION, "TLSv1.2,TLSv1.3");
            break;

        case mxb::ssl_version::TLS13:
            mysql_optionsv(con, MARIADB_OPT_TLS_VERSION, "TLSv1.3");
            break;

        default:
            break;
        }
    }

    const auto& local_address = mxs::Config::get().local_address;

    if (!local_address.empty())
    {
        mysql_optionsv(con, MYSQL_OPT_BIND, local_address.c_str());
    }

    MYSQL* mysql = nullptr;

    if (address[0] == '/')
    {
        mysql = mysql_real_connect(con, nullptr, user, passwd, nullptr, 0, address, flags);
    }
    else
    {
        mysql = mysql_real_connect(con, address, user, passwd, NULL, port, NULL, flags);
    }

    return mysql;
}

MYSQL* mxs_mysql_real_connect(MYSQL* con, SERVER* server, int port, const char* user, const char* passwd)
{
    char yes = 1;
    mysql_optionsv(con, MYSQL_OPT_RECONNECT, &yes);

    bool server_is_db = server->info().is_database();
    if (server_is_db)
    {
        mysql_optionsv(con, MYSQL_INIT_COMMAND, "SET SQL_MODE=''");
        mysql_optionsv(con, MYSQL_INIT_COMMAND, "SET @@session.autocommit=1;");
    }

    auto ssl = server->ssl_config();
    MYSQL* mysql = mxs_mysql_real_connect(con, server->address(), port, user, passwd, ssl);

    if (server_is_db && mysql && mysql_query(mysql, "SET NAMES latin1") != 0)
    {
        MXB_ERROR("Failed to set latin1 character set: %s", mysql_error(mysql));
        mysql = NULL;
    }

    if (mysql)
    {
        if (server_is_db)
        {
            /** Copy the server charset */
            mxs_update_server_charset(mysql, server);
        }

        if (ssl.enabled && mysql_get_ssl_cipher(con) == NULL)
        {
            MXB_ERROR("An encrypted connection to '%s' could not be created, "
                      "ensure that TLS is enabled on the target server.",
                      server->name());
            // Don't close the connection as it is closed elsewhere, just set to NULL
            mysql = NULL;
        }
    }

    return mysql;
}

int mxs_mysql_query(MYSQL* conn, const char* query)
{
    const auto& cnf = mxs::Config::get();
    return maxsql::mysql_query_ex(conn, query,
                                  cnf.query_retries.get(), cnf.query_retry_timeout.get().count());
}

mxs_mysql_name_kind_t mxs_mysql_name_to_pcre(char* pcre,
                                             const char* mysql,
                                             mxs_pcre_quote_approach_t approach)
{
    mxs_mysql_name_kind_t rv = MXS_MYSQL_NAME_WITHOUT_WILDCARD;

    while (*mysql)
    {
        switch (*mysql)
        {
        case '%':
            if (approach == MXS_PCRE_QUOTE_WILDCARD)
            {
                *pcre = '.';
                pcre++;
                *pcre = '*';
            }
            rv = MXS_MYSQL_NAME_WITH_WILDCARD;
            break;

        case '\'':
        case '^':
        case '.':
        case '$':
        case '|':
        case '(':
        case ')':
        case '[':
        case ']':
        case '*':
        case '+':
        case '?':
        case '{':
        case '}':
            *pcre++ = '\\';
            [[fallthrough]];

        default:
            *pcre = *mysql;
        }

        ++pcre;
        ++mysql;
    }

    *pcre = 0;

    return rv;
}

void mxs_mysql_update_server_version(SERVER* dest, MYSQL* source)
{
    // This function should only be called for a live connection.
    const char* version_string = mysql_get_server_info(source);
    unsigned long version_num = mysql_get_server_version(source);
    uint64_t caps = mxq::mysql_get_server_capabilities(source);

    mxb_assert(version_string && version_num != 0);
    dest->set_version(SERVER::BaseType::MARIADB, version_num, version_string, caps);
}

namespace maxscale
{

std::unique_ptr<mxb::QueryResult> execute_query(MYSQL* conn, const std::string& query,
                                                std::string* errmsg_out, unsigned int* errno_out)
{
    using mxb::QueryResult;
    std::unique_ptr<QueryResult> rval;
    MYSQL_RES* result = NULL;
    if (mxs_mysql_query(conn, query.c_str()) == 0 && (result = mysql_store_result(conn)) != NULL)
    {
        rval = std::unique_ptr<QueryResult>(new mxq::MariaDBQueryResult(result));
    }
    else
    {
        if (errmsg_out)
        {
            *errmsg_out = mxb::string_printf("Query '%s' failed: '%s'.", query.c_str(), mysql_error(conn));
        }

        if (errno_out)
        {
            *errno_out = mysql_errno(conn);
        }
    }

    return rval;
}
}

void mxs_update_server_charset(MYSQL* mysql, SERVER* server)
{
    const char* CHARSET_QUERY =
        "SELECT id, collation_name, character_set_name FROM information_schema.collations "
        "WHERE collation_name=@@global.collation_server "
        "UNION "
        "SELECT id, collation_name, character_set_name FROM information_schema.collations";

    if (mxs_mysql_query(mysql, CHARSET_QUERY) == 0)
    {
        if (auto res = mysql_use_result(mysql))
        {
            if (auto row = mysql_fetch_row(res))
            {
                if (row[0])
                {
                    auto charset = atoi(row[0]);

                    if (server->charset() != charset)
                    {
                        MXB_NOTICE("Server '%s' charset: %s", server->name(), row[1]);
                        server->set_charset(charset);
                    }
                }
            }

            // The remaining rows contain the collation IDs and character set names
            std::map<int, mxs::Collation> collations;

            while (auto row = mysql_fetch_row(res))
            {
                // There are some collations that have null values in the character set field.
                if (row[0] && row[1] && row[2])
                {
                    if (int id = atoi(row[0]))
                    {
                        collations.emplace(atoi(row[0]), mxs::Collation {row[1], row[2]});
                    }
                }
            }

            server->set_collations(std::move(collations));

            mysql_free_result(res);
        }
    }
}
