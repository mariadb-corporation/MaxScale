/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-03
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

#include <maxscale/mysql_utils.hh>

#include <string.h>
#include <strings.h>
#include <mysql.h>

#include <maxbase/alloc.h>
#include <maxbase/atomic.hh>
#include <maxbase/format.hh>
#include <maxsql/mariadb.hh>
#include <maxsql/mariadb_connector.hh>
#include <maxscale/config.hh>

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
        MXS_ERROR("Failed to set latin1 character set: %s", mysql_error(mysql));
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
            MXS_ERROR("An encrypted connection to '%s' could not be created, "
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

const char* mxs_mysql_get_value(MYSQL_RES* result, MYSQL_ROW row, const char* key)
{
    MYSQL_FIELD* f = mysql_fetch_fields(result);
    int nfields = mysql_num_fields(result);

    for (int i = 0; i < nfields; i++)
    {
        if (strcasecmp(f[i].name, key) == 0)
        {
            return row[i];
        }
    }

    return NULL;
}

bool mxs_mysql_trim_quotes(char* s)
{
    bool dequoted = true;

    char* i = s;
    char* end = s + strlen(s);

    // Remove space from the beginning
    while (*i && isspace(*i))
    {
        ++i;
    }

    if (*i)
    {
        // Remove space from the end
        while (isspace(*(end - 1)))
        {
            *(end - 1) = 0;
            --end;
        }

        mxb_assert(end > i);

        char quote;

        switch (*i)
        {
        case '\'':
        case '"':
        case '`':
            quote = *i;
            ++i;
            break;

        default:
            quote = 0;
        }

        if (quote)
        {
            --end;

            if (*end == quote)
            {
                *end = 0;

                memmove(s, i, end - i + 1);
            }
            else
            {
                dequoted = false;
            }
        }
        else if (i != s)
        {
            memmove(s, i, end - i + 1);
        }
    }
    else
    {
        *s = 0;
    }

    return dequoted;
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

        // Flowthrough
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
    mxb_assert(version_string && version_num != 0);
    dest->set_version(version_num, version_string);
}

namespace maxscale
{

std::unique_ptr<mxq::QueryResult> execute_query(MYSQL* conn, const std::string& query,
                                                std::string* errmsg_out, unsigned int* errno_out)
{
    using mxq::QueryResult;
    std::unique_ptr<QueryResult> rval;
    if (mxs_mysql_query(conn, query.c_str()) == 0)
    {
        // Query (or entire multiquery) succeeded. Loop for more results in case of multiquery.
        do
        {
            MYSQL_RES* result = mysql_store_result(conn);
            if (result)
            {
                if (rval)
                {
                    // Return just the first resultset.
                    mysql_free_result(result);
                }
                else
                {
                    rval = std::unique_ptr<QueryResult>(new mxq::MariaDBQueryResult(result));
                }
            }
        }
        while (mysql_next_result(conn) == 0);
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

const char* mxs_response_to_string(GWBUF* pPacket)
{
    thread_local std::string rv;

    std::stringstream ss;

    mxs::Buffer b(pPacket);
    int nRemaining = b.length();
    auto it = b.begin();

    while (nRemaining > MYSQL_HEADER_LEN + 1)
    {
        if (!ss.str().empty())
        {
            ss << "\n";
        }

        uint8_t header[MYSQL_HEADER_LEN + 1];

        auto start = it;
        auto end = std::next(it, sizeof(header));
        std::copy(it, end, header);
        it = end;

        uint32_t payload_len = MYSQL_GET_PAYLOAD_LEN(header);
        uint32_t packet_len = MYSQL_HEADER_LEN + payload_len;
        uint32_t packet_no = MYSQL_GET_PACKET_NO(header);
        uint32_t command = MYSQL_GET_COMMAND(header);

        ss << "Packet no: " << packet_no << ", Payload len: " << payload_len;

        switch (command)
        {
        case 0x00:
            ss << ", Command : OK";
            break;

        case 0xff:
            {
                ss << ", Command : ERR";

                uint8_t error[payload_len];
                error[0] = *it;

                end = std::next(it, sizeof(error) - 1);     // -1 due to the 1 in 'header' above.
                std::copy(it, end, error + 1);

                uint32_t error_code = gw_mysql_get_byte2(&error[1]);

                ss << ", Code: " << error_code;

                const int message_index = 1 + 2 + 1 + 5;
                uint8_t* pMessage = &error[message_index];
                int message_len = payload_len - message_index;

                ss << ", Message : ";

                ss.write(reinterpret_cast<const char*>(pMessage), message_len);
            }
            break;

        case 0xfb:
            ss << ", Command : GET_MORE_CLIENT_DATA";
            break;

        default:
            ss << ", Command : Result Set";
        }

        it = std::next(start, MYSQL_HEADER_LEN + payload_len);

        nRemaining -= MYSQL_HEADER_LEN;
        nRemaining -= payload_len;
    }

    b.release();

    rv = ss.str();

    return rv.c_str();
}

void mxs_update_server_charset(MYSQL* mysql, SERVER* server)
{
    // NOTE: The order in which these queries are run must have the newer versions first and the older ones
    // later. Do not reorder them!
    auto QUERIES =
    {
        // For MariaDB 10.10 and newer. The information_schema.COLLATIONS table now has rows with NULL ID
        // values and the value of @@global.collation_server is no longer found there. Instead, we have to
        // query a different table.
        "SELECT ID, FULL_COLLATION_NAME FROM information_schema.COLLATION_CHARACTER_SET_APPLICABILITY"
        "WHERE FULL_COLLATION_NAME = @@global.collation_server",

        // For old MariaDB versions that do not have information_schema.COLLATION_CHARACTER_SET_APPLICABILITY
        "SELECT id, @@global.collation_server FROM information_schema.collations "
        "WHERE collation_name=@@global.collation_server",
    };

    std::string charset_name;
    int charset = 0;

    for (const char* charset_query : QUERIES)
    {
        if (mxs_mysql_query(mysql, charset_query) == 0)
        {
            if (auto res = mysql_use_result(mysql))
            {
                if (auto row = mysql_fetch_row(res))
                {
                    if (row[0])
                    {
                        charset = atoi(row[0]);

                        if (row[1])
                        {
                            charset_name = row[1];
                        }
                    }
                }

                mysql_free_result(res);
            }

            if (charset)
            {
                break;
            }
        }
    }

    if (server->charset() != charset)
    {
        // The ID values returned for newer collations are two byte values and we have to map them to a single
        // byte value. The X_general_ci values all have a ID that's below 255 and this is what MariaDB sends
        // when the real collation won't fit into the one byte value. In essence, the collation byte should
        // really be interpreted as a character set byte and not a true collation one.

        // 800-8FF 2048-2303  utf8mb3_uca1400 (pad/nopad,as/ai,cs/ci)
        if (charset >= 2048 && charset <= 2303)
        {
            charset = 33;   // utf8mb3_general_ci
        }
        // 900-9FF 2304-2559  utf8mb4_uca1400 (pad/nopad,as/ai,cs/ci)
        else if (charset >= 2304 && charset <= 2559)
        {
            charset = 45;   // utf8mb4_general_ci
        }
        // A00-AFF 2560-2815  ucs2_uca1400    (pad/nopad,as/ai,cs/ci)
        else if (charset >= 2560 && charset <= 2815)
        {
            charset = 35;   // ucs2_general_ci
        }
        // B00-BFF 2816-3071  utf16_uca1400   (pad/nopad,as/ai,cs/ci)
        else if (charset >= 2816 && charset <= 3071)
        {
            charset = 54;   // utf16_general_ci
        }
        // C00-CFF 3072-3328  utf32_uca1400   (pad/nopad,as/ai,cs/ci)
        else if (charset >= 3072 && charset <= 3328)
        {
            charset = 60;   // utf32_general_ci
        }

        MXS_NOTICE("Server '%s' charset: %s", server->name(), charset_name.c_str());
        server->set_charset(charset);
    }
}
