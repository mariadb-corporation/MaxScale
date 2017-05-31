#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>
#include <maxscale/customparser.hh>
#include <maxscale/protocol/mysql.h>


namespace maxscale
{

class SetSqlModeParser : public CustomParser
{
public:
    enum sql_mode_t
    {
        DEFAULT,   // "set sql_mode=DEFAULT"
        ORACLE,    // "set sql_mode=ORACLE", "set sql_mode='PIPES_AS_CONCAT,ORACLE', autocommit=false", etc.
        SOMETHING  // "set sql_mode=PIPES_AS_CONCAT"
    };

    enum result_t
    {
        ERROR,           // Some fatal error occurred; mem alloc failed, parsing failed, etc.
        IS_SET_SQL_MODE, // The COM_QUERY is "set sql_mode=..."
        NOT_SET_SQL_MODE // The COM_QUERY is NOT "set sql_mode=..."
    };

    enum
    {
        UNUSED_FIRST = 0xFF,
        TK_DEFAULT,
        TK_GLOBAL,
        TK_GLOBAL_VAR,
        TK_ORACLE,
        TK_SESSION,
        TK_SESSION_VAR,
        TK_SET,
        TK_SQL_MODE,
    };

    SetSqlModeParser()
    {
    }

    /**
     * Return whether the statement is a "SET SQL_MODE=" statement and if so,
     * whether the state is ORACLE, DEFAULT or something else.
     *
     * @param ppBuffer   Address of pointer to buffer containing statement.
     *                   The GWBUF must contain a complete statement, but the
     *                   buffer need not be contiguous.
     * @param pSql_mode  Pointer to variable receiving the sql_mode state, if
     *                   the statement is a "SET SQL_MODE=" statement.
     *
     * @return ERROR            if a fatal error occurred during parsing
     *         IS_SET_SQL_MODE  if the statement is a "SET SQL_MODE=" statement
     *         NOT_SET_SQL_MODE if the statement is not a "SET SQL_MODE="
     *                          statement
     *
     * @attention If the result cannot be deduced without parsing the statement,
     *            then the buffer will be made contiguous and the value of
     *            @c *ppBuffer will be updated accordingly.
     */
    result_t get_sql_mode(GWBUF** ppBuffer, sql_mode_t* pSql_mode)
    {
        result_t rv = NOT_SET_SQL_MODE;

        GWBUF* pBuffer = *ppBuffer;

        ss_dassert(gwbuf_length(pBuffer) >= MYSQL_HEADER_LEN);

        size_t buf_len = GWBUF_LENGTH(pBuffer);
        size_t payload_len;
        if (buf_len >= MYSQL_HEADER_LEN)
        {
            // The first buffer in the chain contains the header so we
            // can read the length directly.
            payload_len = MYSQL_GET_PAYLOAD_LEN(GWBUF_DATA(pBuffer));
        }
        else
        {
            // The first buffer in the chain does not contain the full
            // header so we need to copy it first.
            uint8_t header[MYSQL_HEADER_LEN];
            gwbuf_copy_data(pBuffer, 0, sizeof(header), header);
            payload_len = MYSQL_GET_PAYLOAD_LEN(header);
        }

        if (payload_len >= 20) // sizeof(command byte) + strlen("SET sql_mode=ORACLE"), the minimum needed.
        {
            // We need 4 bytes from the payload to deduce whether more investigations are needed.
            uint8_t payload[4];
            uint8_t* pPayload;

            if (buf_len >= MYSQL_HEADER_LEN + sizeof(payload))
            {
                // Enough data in the first buffer of the chain, we can access directly.
                pPayload = GWBUF_DATA(pBuffer) + MYSQL_HEADER_LEN;
            }
            else
            {
                // Not enough, we copy what we need.
                gwbuf_copy_data(pBuffer, MYSQL_HEADER_LEN, sizeof(payload), payload);
                pPayload = payload;
            }

            uint8_t command = pPayload[0];

            if (command == MYSQL_COM_QUERY)
            {
                const uint8_t* pStmt = &pPayload[1];

                if (is_alpha(*pStmt))
                {
                    // First character is alphabetic, we can check whether it is "SET".
                    if (is_set(pStmt))
                    {
                        // It is, so we must parse further and must therefore ensure that
                        // the buffer is contiguous. We get the same buffer back if it
                        // already is.
                        pBuffer = gwbuf_make_contiguous(*ppBuffer);

                        if (pBuffer)
                        {
                            *ppBuffer = pBuffer;
                            initialize(pBuffer);

                            rv = parse(pSql_mode);
                        }
                        else
                        {
                            rv = ERROR;
                        }
                    }
                }
                else
                {
                    // If the first character is not an alphabetic character we assume there
                    // is a comment and make the buffer contiguous to make it possible to
                    // efficiently bypass the whitespace.
                    pBuffer = gwbuf_make_contiguous(*ppBuffer);

                    if (pBuffer)
                    {
                        *ppBuffer = pBuffer;
                        initialize(pBuffer);

                        bypass_whitespace();

                        if (is_set(m_pI))
                        {
                            rv = parse(pSql_mode);
                        }
                    }
                    else
                    {
                        rv = ERROR;
                    }
                }
            }
        }

        return rv;
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

    /**
     * Returns a @c result_t as a string.
     *
     * @param result_t  A result.
     *
     * @return The corresponding string.
     */
    static const char* to_string(result_t result)
    {
        switch (result)
        {
        case ERROR:
            return "ERROR";

        case IS_SET_SQL_MODE:
            return "IS_SET_SQL_MODE";

        case NOT_SET_SQL_MODE:
            return "NOT_SET_SQL_MODE";

        default:
            ss_dassert(!true);
            return "UNKNOWN";
        }
    }

private:
    static bool is_set(const char* pStmt)
    {
        return
            (pStmt[0] == 's' || pStmt[0] == 'S') &&
            (pStmt[1] == 'e' || pStmt[1] == 'E') &&
            (pStmt[2] == 't' || pStmt[2] == 'T');
    }

    static bool is_set(const uint8_t* pStmt)
    {
        return is_set(reinterpret_cast<const char*>(pStmt));
    }

    static bool is_error(result_t rv)
    {
        return (rv == ERROR);
    }

    result_t initialize(GWBUF* pBuffer)
    {
        ss_dassert(GWBUF_IS_CONTIGUOUS(pBuffer));

        result_t rv = ERROR;

        char* pSql;
        if (modutil_extract_SQL(pBuffer, &pSql, &m_len))
        {
            m_pSql = pSql;
            m_pI = m_pSql;
            m_pEnd = m_pI + m_len;
        }

        return ERROR;
    }

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

    void consume_value()
    {
        // Consumes everything until a ',' outside of a commented string, or eol is
        // encountered.
        bool rv = false;
        bool consumed = false;

        while ((m_pI < m_pEnd) && (*m_pI != ','))
        {
            switch (*m_pI)
            {
            case '\'':
            case '"':
            case '`':
                {
                    char quote = *m_pI;
                    ++m_pI;
                    while ((m_pI < m_pEnd) && (*m_pI != quote))
                    {
                        ++m_pI;
                    }
                }
                break;

            default:
                ++m_pI;
            }
        }
    }

    result_t parse(sql_mode_t* pSql_mode)
    {
        result_t rv = NOT_SET_SQL_MODE;
        token_t token = next_token();

        switch (token)
        {
        case TK_SET:
            rv = parse_set(pSql_mode);
            break;

        case PARSER_EXHAUSTED:
            log_exhausted();
            break;

        case PARSER_UNKNOWN_TOKEN:
        default:
            log_unexpected();
            break;
        }

        return rv;
    }

    result_t parse_set(sql_mode_t* pSql_mode)
    {
        result_t rv = NOT_SET_SQL_MODE;

        char c;

        do
        {
            token_t token = next_token();

            switch (token)
            {
            case TK_GLOBAL:
                rv = parse_set(pSql_mode);
                break;

            case TK_SESSION:
                rv = parse_set(pSql_mode);
                break;

            case TK_GLOBAL_VAR:
            case TK_SESSION_VAR:
                if (next_token() == '.')
                {
                    rv = parse_set(pSql_mode);
                }
                else
                {
                    rv = ERROR;
                }
                break;

            case TK_SQL_MODE:
                if (next_token() == '=')
                {
                    rv = parse_set_sql_mode(pSql_mode);
                }
                else
                {
                    rv = ERROR;
                }
                break;

            case PARSER_EXHAUSTED:
                log_exhausted();
                rv = ERROR;
                break;

            case PARSER_UNKNOWN_TOKEN:
                // Might be something like "SET A=B, C=D, SQL_MODE=ORACLE", so we first consume
                // the identifier and if it is followed by a "=" we consume the value.
                {
                    char c;
                    if (consume_id())
                    {
                        bypass_whitespace();

                        if (peek_current_char(&c) && (c == '='))
                        {
                            ++m_pI;
                            consume_value();
                        }
                    }
                    else
                    {
                        log_unexpected();
                        rv = ERROR;
                    }
                }
                break;

            default:
                log_unexpected();
                rv = ERROR;
                break;
            }

            c = 0;

            if (rv != ERROR)
            {
                bypass_whitespace();

                if (peek_current_char(&c))
                {
                    if (c == ',')
                    {
                        ++m_pI;
                    }
                    else
                    {
                        c = 0;
                    }
                }
                else
                {
                    c = 0;
                }
            }
        }
        while (c == ',');

        return rv;
    }

    result_t parse_set_sql_mode(sql_mode_t* pSql_mode)
    {
        result_t rv = IS_SET_SQL_MODE;

        token_t token = next_token();

        switch (token)
        {
        case '\'':
        case '"':
        case '`':
            rv = parse_set_sql_mode_string(token, pSql_mode);
            break;

        case TK_DEFAULT:
            *pSql_mode = DEFAULT;
            break;

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
                rv = ERROR;
            }
            break;

        default:
            rv = ERROR;
        }

        return rv;
    }

    result_t parse_set_sql_mode_string(char quote, sql_mode_t* pSql_mode)
    {
        result_t rv = IS_SET_SQL_MODE;

        char c;

        do
        {
            rv = parse_set_sql_mode_setting(pSql_mode);

            if (!is_error(rv))
            {
                bypass_whitespace();

                if (peek_current_char(&c) && (c == ','))
                {
                    ++m_pI;
                }
            }
        }
        while (!is_error(rv) && (c == ','));

        return rv;
    }

    result_t parse_set_sql_mode_setting(sql_mode_t* pSql_mode)
    {
        result_t rv = IS_SET_SQL_MODE;

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
                rv = ERROR;
            }
            break;

        case PARSER_EXHAUSTED:
            log_exhausted();
            rv = ERROR;
            break;

        default:
            log_unexpected();
            rv = ERROR;
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
            case '@':
                if (is_next_alpha('S', 2))
                {
                    token = expect_token(MXS_CP_EXPECT_TOKEN("@@SESSION"), TK_SESSION_VAR);
                }
                else if (is_next_alpha('G', 2))
                {
                    token = expect_token(MXS_CP_EXPECT_TOKEN("@@GLOBAL"), TK_GLOBAL_VAR);
                }
                else if (is_next_alpha('L', 2))
                {
                    token = expect_token(MXS_CP_EXPECT_TOKEN("@@LOCAL"), TK_SESSION_VAR);
                }
                break;

            case '.':
            case '\'':
            case '"':
            case '`':
            case ',':
            case '=':
                token = *m_pI;
                ++m_pI;
                break;

            case 'd':
            case 'D':
                token = expect_token(MXS_CP_EXPECT_TOKEN("DEFAULT"), TK_DEFAULT);
                break;

            case 'g':
            case 'G':
                token = expect_token(MXS_CP_EXPECT_TOKEN("GLOBAL"), TK_GLOBAL);
                break;

            case 'l':
            case 'L':
                token = expect_token(MXS_CP_EXPECT_TOKEN("LOCAL"), TK_SESSION);
                break;

            case 'o':
            case 'O':
                token = expect_token(MXS_CP_EXPECT_TOKEN("ORACLE"), TK_ORACLE);
                break;

            case 's':
            case 'S':
                if (is_next_alpha('E'))
                {
                    if (is_next_alpha('S', 2))
                    {
                        token = expect_token(MXS_CP_EXPECT_TOKEN("SESSION"), TK_SESSION);
                    }
                    else
                    {
                        token = expect_token(MXS_CP_EXPECT_TOKEN("SET"), TK_SET);
                    }
                }
                else if (is_next_alpha('Q'))
                {
                    token = expect_token(MXS_CP_EXPECT_TOKEN("SQL_MODE"), TK_SQL_MODE);
                }
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

}
