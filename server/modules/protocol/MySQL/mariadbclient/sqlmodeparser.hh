/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
 #pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/customparser.hh>
#include <maxscale/protocol/mysql.h>


class SqlModeParser : public maxscale::CustomParser
{
public:
    enum sql_mode_t
    {
        DEFAULT,   // "set sql_mode=DEFAULT"
        ORACLE,    // "set sql_mode=ORACLE", "set sql_mode='PIPES_AS_CONCAT,ORACLE', autocommit=false", etc.
        SOMETHING  // "set sql_mode=PIPES_AS_CONCAT"
    };

    enum
    {
        UNUSED_FIRST = 0xFF,
        TK_DEFAULT,
        TK_ORACLE,
    };

    SqlModeParser()
    {
    }

    /**
     * Given the trimmed value from the right of a "SET SQL_MODE=..." statement
     * return whether SQL_MODE is set to ORACLE or DEFAULT.
     *
     * @param pBegin  Beginning of string.
     * @param pEnd    One past end of string.
     *
     * @return DEFAULT   if the value is DEFAULT,
     *         ORACLE    if the string contains ORACLE,
     *         SOMETHING otherwise.
     */
    sql_mode_t get_sql_mode(const char* pBegin, const char* pEnd)
    {
        sql_mode_t sql_mode = SOMETHING;

        m_pSql = pBegin;
        m_pI   = m_pSql;
        m_pEnd = pEnd;

        return parse();
    }

    /**
     * Returns a @c sql_mode_t as a string.
     *
     * @param sql_mode  An SQL mode.
     *
     * @return The corresponding string.
     */
    static const char* to_string(sql_mode_t sql_mode)
    {
        switch (sql_mode)
        {
        case DEFAULT:
            return "DEFAULT";

        case ORACLE:
            return "ORACLE";

        case SOMETHING:
            return "SOMETHING";

        default:
            ss_dassert(!true);
            return "UNKNOWN";
        }
    }

private:
    bool consume_id()
    {
        // Consumes "[a-zA-Z]([a-zA-Z0-9_])*

        bool rv = false;

        if (is_alpha(*m_pI))
        {
            rv = true;

            ++m_pI;

            while ((m_pI < m_pEnd) && (is_alpha(*m_pI) || is_number(*m_pI) || (*m_pI == '_')))
            {
                ++m_pI;
            }
        }

        return rv;
    }

    sql_mode_t parse()
    {
        sql_mode_t rv = SOMETHING;

        token_t token = next_token();

        switch (token)
        {
        case '\'':
        case '"':
        case '`':
            rv = parse_string(token);
            break;

        case TK_DEFAULT:
            rv = DEFAULT;
            break;

        case TK_ORACLE:
            rv = ORACLE;
            break;

        case PARSER_UNKNOWN_TOKEN:
        default:
            ;
        }

        return rv;
    }

    sql_mode_t parse_string(char quote)
    {
        sql_mode_t rv = SOMETHING;

        bool parsed;
        char c;

        do
        {
            parsed = parse_setting(&rv);

            if (parsed)
            {
                bypass_whitespace();

                if (peek_current_char(&c) && (c == ','))
                {
                    ++m_pI;
                }
            }
        }
        while (parsed && (c == ','));

        return rv;
    }

    bool parse_setting(sql_mode_t* pSql_mode)
    {
        bool rv = true;

        token_t token = next_token();

        switch (token)
        {
        case TK_ORACLE:
            *pSql_mode = ORACLE;
            break;

        case PARSER_UNKNOWN_TOKEN:
            if (consume_id())
            {
                *pSql_mode = SOMETHING;
            }
            else
            {
                rv = false;
            }
            break;

        case PARSER_EXHAUSTED:
            log_exhausted();
            rv = false;
            break;

        default:
            log_unexpected();
            rv = false;
        }

        return rv;
    }

    token_t next_token(token_required_t required = TOKEN_NOT_REQUIRED)
    {
        token_t token = PARSER_UNKNOWN_TOKEN;

        bypass_whitespace();

        if (m_pI == m_pEnd)
        {
            token = PARSER_EXHAUSTED;
        }
        else if (*m_pI == ';')
        {
            ++m_pI;

            while ((m_pI != m_pEnd) && isspace(*m_pI))
            {
                ++m_pI;
            }

            if (m_pI != m_pEnd)
            {
                MXS_WARNING("Non-space data found after semi-colon: '%.*s'.",
                            (int)(m_pEnd - m_pI), m_pI);
            }

            token = PARSER_EXHAUSTED;
        }
        else
        {
            switch (*m_pI)
            {
            case '\'':
            case '"':
            case '`':
            case ',':
                token = *m_pI;
                ++m_pI;
                break;

            case 'd':
            case 'D':
                token = expect_token(MXS_CP_EXPECT_TOKEN("DEFAULT"), TK_DEFAULT);
                break;

            case 'o':
            case 'O':
                token = expect_token(MXS_CP_EXPECT_TOKEN("ORACLE"), TK_ORACLE);
                break;

            default:
                ;
            }
        }

        if ((token == PARSER_EXHAUSTED) && (required == TOKEN_REQUIRED))
        {
            log_exhausted();
        }

        return token;
    }
};
