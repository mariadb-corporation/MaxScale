/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include "xrouter.hh"
#include "xroutersession.hh"

#include <maxscale/protocol/postgresql/scram.hh>

namespace
{
/**
 * Implementation of strcasestr for string_views
 */
std::string_view::iterator find_token(std::string_view::iterator begin,
                                      std::string_view::iterator end,
                                      std::string_view token)
{
    return std::search(begin, end, token.begin(), token.end(), [](auto lhs, auto rhs){
        return tolower(lhs) == tolower(rhs);
    });
}

/**
 * Extract the raw password from the SQL and convert it into the salted format that Postgres stores it in
 */
void presalt_password(const char* start, const char* ptr, const char* end,
                      const mxs::ProtocolModule& protocol, GWBUF& packet)
{
    // Skip whitespace
    while (ptr != end && std::isspace(*ptr))
    {
        ++ptr;
    }

    // TODO: Add support for extended strings that support backslash escapes

    if (ptr != end && *ptr == '\'')
    {
        // Skip the quote and store the start of the SQL statement
        ++ptr;
        std::string_view prefix{start, (size_t)(ptr - start)};
        std::string pw;
        bool stop = false;

        while (ptr != end)
        {
            if (*ptr == '\'')
            {
                if (stop)
                {
                    // Two single quotes, convert it into one quote character
                    stop = false;
                    pw.push_back(*ptr);
                }
                else
                {
                    // Possible end of the string constant, stop processing if the next character is not a
                    // single quote.
                    stop = true;
                }
            }
            else if (stop)
            {
                // End of string constant
                break;
            }
            else
            {
                pw.push_back(*ptr);
            }

            ++ptr;
        }

        if (stop)
        {
            --ptr;
            mxb_assert(ptr != end);

            std::string_view suffix{ptr, (size_t)(end - ptr)};
            packet = protocol.make_query(mxb::cat(prefix, pg::salt_password(pw), suffix));
        }
    }
}

/**
 * Pre-salts a password by replacing the plaintext password with the SCRAM-SHA-256 version of it.
 * The pre-salting makes sure that the same salt is used on all of the Postgres servers. This allows the
 * ClientKey that is extracted by MaxScale during the authentication to be reused on multiple servers. Without
 * it, only the server from which the users were loaded from would accept the authentication.
 */
void handle_create_user(const mxs::ProtocolModule& protocol, const mxs::Parser& parser, GWBUF& packet)
{
    if (parser.get_operation(packet) == mxs::sql::OP_CREATE)
    {
        std::string_view sql = protocol.get_sql(packet);
        std::string_view TOK_ROLE = "ROLE";
        std::string_view TOK_USER = "USER";
        std::string_view TOK_PASSWORD = "PASSWORD";
        std::string_view::const_iterator role;

        if ((role = find_token(sql.begin(), sql.end(), TOK_USER)) != sql.end()
            || (role = find_token(sql.begin(), sql.end(), TOK_ROLE)) != sql.end())
        {
            if (auto ptr = find_token(role, sql.end(), TOK_PASSWORD); ptr != sql.end())
            {
                ptr += TOK_PASSWORD.size();
                presalt_password(sql.begin(), ptr, sql.end(), protocol, packet);
            }
        }
    }
}
}

void XgresSession::preprocess(GWBUF& packet)
{
    handle_create_user(protocol(), parser(), packet);
}

std::string XgresSession::main_sql() const
{
    return "SET xgres.fdw_mode = 'pushdown'";
}

std::string XgresSession::secondary_sql() const
{
    return "SET xgres.fdw_mode = 'import'";
}

std::string XgresSession::lock_sql(std::string_view lock_id) const
{
    return mxb::cat("SELECT pg_advisory_lock(", lock_id, ")");
}

std::string XgresSession::unlock_sql(std::string_view lock_id) const
{
    return mxb::cat("SELECT pg_advisory_unlock(", lock_id, ")");
}
