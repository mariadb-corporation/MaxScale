/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <ctype.h>
#include <maxscale/protocol/mariadb/customparser.hh>
#include <maxscale/parser.hh>

namespace maxscale
{

#define TBP_EXPECT_TOKEN(string_literal) string_literal, (sizeof(string_literal) - 1)

// For debugging purposes.
// #define TBP_LOG_UNEXPECTED_AND_EXHAUSTED
#undef TBP_LOG_UNEXPECTED_AND_EXHAUSTED

/**
 * @class TrxBoundaryParser
 *
 * @ TrxBoundaryParser is a class capable of parsing and returning the
 * correct type mask of statements affecting the transaction state and
 * autocommit mode.
 *
 * The class is intended to be used in context where the performance is
 * of utmost importance; consequently it is defined in its entirety
 * in the header to allow for aggressive inlining.
 */
class TrxBoundaryParser : public maxscale::CustomParser
{
    TrxBoundaryParser(const TrxBoundaryParser&);
    TrxBoundaryParser& operator=(const TrxBoundaryParser&);

public:
    enum token_t
    {
        // TK_ would conflict with tokens of sqlite.
        TOK_AUTOCOMMIT,
        TOK_BEGIN,
        TOK_COMMA,
        TOK_COMMIT,
        TOK_COMMITTED,
        TOK_CONSISTENT,
        TOK_DOT,
        TOK_END,
        TOK_EQ,
        TOK_FALSE,
        TOK_GLOBAL,
        TOK_GLOBAL_VAR,
        TOK_ISOLATION,
        TOK_LEVEL,
        TOK_ONE,
        TOK_ONLY,
        TOK_READ,
        TOK_REPEATABLE,
        TOK_ROLLBACK,
        TOK_SESSION,
        TOK_SESSION_VAR,
        TOK_SET,
        TOK_SERIALIZABLE,
        TOK_SNAPSHOT,
        TOK_START,
        TOK_TRANSACTION,
        TOK_TRUE,
        TOK_UNCOMMITTED,
        TOK_WITH,
        TOK_WORK,
        TOK_WRITE,
        TOK_XA,
        TOK_ZERO,

        PARSER_UNKNOWN_TOKEN,
        PARSER_EXHAUSTED,
    };

    /**
     * TrxBoundaryParser is not thread-safe. As a very lightweight class,
     * the intention is that an instance is created on the stack whenever
     * parsing needs to be performed.
     *
     * @code
     *     void f(GWBUF *pBuf)
     *     {
     *         TrxBoundaryParser tbp;
     *
     *         uint32_t type_mask = tbp.parse(pBuf);
     *         ...
     *     }
     * @endcode
     */
    TrxBoundaryParser()
    {
    }

    /**
     * Return the type mask of a statement, provided the statement affects
     * transaction state or autocommit mode.
     *
     * @param pSql  SQL statement.
     * @param len   Length of pSql.
     *
     * @return The corresponding type mask or 0, if the statement does not
     *         affect transaction state or autocommit mode.
     */
    uint32_t type_mask_of(const char* pSql, size_t len)
    {
        uint32_t type_mask = 0;

        m_pSql = pSql;
        m_len = len;

        m_pI = m_pSql;
        m_pEnd = m_pI + m_len;

        return parse();
    }

    /**
     * Return the type mask of a statement, provided the statement affects
     * transaction state or autocommit mode.
     *
     * @param pBuf A COM_QUERY
     *
     * @return The corresponding type mask or 0, if the statement does not
     *         affect transaction state or autocommit mode.
     */
    uint32_t type_mask_of(std::string_view sql)
    {
        uint32_t type_mask = 0;

        if (!sql.empty())
        {
            m_pSql = sql.data();
            m_len = sql.length();
            m_pI = m_pSql;
            m_pEnd = m_pI + m_len;

            type_mask = parse();
        }

        return type_mask;
    }

private:
    enum token_required_t
    {
        TOKEN_REQUIRED,
        TOKEN_NOT_REQUIRED,
    };

    void log_unexpected()
    {
#ifdef TBP_LOG_UNEXPECTED_AND_EXHAUSTED
        MXB_NOTICE("Transaction tracking: In statement '%.*s', unexpected token at '%.*s'.",
                   (int)m_len,
                   m_pSql,
                   (int)(m_pEnd - m_pI),
                   m_pI);
#endif
    }

    void log_exhausted()
    {
#ifdef TBP_LOG_UNEXPECTED_AND_EXHAUSTED
        MXB_NOTICE("Transaction tracking: More tokens expected in statement '%.*s'.", (int)m_len, m_pSql);
#endif
    }

    uint32_t parse()
    {
        uint32_t type_mask = 0;

        token_t token = next_token();

        switch (token)
        {
        case TOK_BEGIN:
            type_mask = parse_begin(type_mask);
            break;

        case TOK_COMMIT:
            type_mask = parse_commit(type_mask);
            break;

        case TOK_ROLLBACK:
            type_mask = parse_rollback(type_mask);
            break;

        case TOK_START:
            type_mask = parse_start(type_mask);
            break;

        case TOK_SET:
            type_mask = parse_set(0);
            break;

        case TOK_XA:
            type_mask = parse_xa(type_mask);
            break;

        default:
            ;
        }

        return type_mask;
    }

    uint32_t parse_begin(uint32_t type_mask)
    {
        type_mask |= mxs::sql::TYPE_BEGIN_TRX;

        token_t token = next_token();

        switch (token)
        {
        case TOK_WORK:
            type_mask = parse_work(type_mask);
            break;

        case PARSER_EXHAUSTED:
            break;

        default:
            type_mask = 0;
            log_unexpected();
        }

        return type_mask;
    }

    uint32_t parse_commit(uint32_t type_mask)
    {
        type_mask |= mxs::sql::TYPE_COMMIT;

        token_t token = next_token();

        switch (token)
        {
        case TOK_WORK:
            type_mask = parse_work(type_mask);
            break;

        case PARSER_EXHAUSTED:
            break;

        default:
            type_mask = 0;
            log_unexpected();
        }

        return type_mask;
    }

    uint32_t parse_only(uint32_t type_mask)
    {
        type_mask |= mxs::sql::TYPE_READ;

        token_t token = next_token();

        switch (token)
        {
        case TOK_COMMA:
            type_mask = parse_transaction(type_mask);
            break;

        case PARSER_EXHAUSTED:
            break;

        default:
            type_mask = 0;
            log_unexpected();
        }

        return type_mask;
    }

    uint32_t parse_read(uint32_t type_mask)
    {
        token_t token = next_token(TOKEN_REQUIRED);

        switch (token)
        {
        case TOK_ONLY:
            type_mask = parse_only(type_mask);
            break;

        case TOK_WRITE:
            type_mask = parse_write(type_mask);
            break;

        case PARSER_EXHAUSTED:
            type_mask = 0;
            break;

        default:
            type_mask = 0;
            log_unexpected();
        }

        return type_mask;
    }

    uint32_t parse_rollback(uint32_t type_mask)
    {
        type_mask |= mxs::sql::TYPE_ROLLBACK;

        token_t token = next_token();

        switch (token)
        {
        case TOK_WORK:
            type_mask = parse_work(type_mask);
            break;

        case PARSER_EXHAUSTED:
            break;

        default:
            type_mask = 0;
            log_unexpected();
        }

        return type_mask;
    }

    uint32_t parse_set_autocommit(uint32_t type_mask)
    {
        token_t token = next_token(TOKEN_REQUIRED);

        switch (token)
        {
        case TOK_EQ:
            token = next_token(TOKEN_REQUIRED);
            if (token == TOK_ONE || token == TOK_TRUE)
            {
                type_mask |= (mxs::sql::TYPE_COMMIT | mxs::sql::TYPE_ENABLE_AUTOCOMMIT);
            }
            else if (token == TOK_ZERO || token == TOK_FALSE)
            {
                type_mask = (mxs::sql::TYPE_BEGIN_TRX | mxs::sql::TYPE_DISABLE_AUTOCOMMIT);
            }
            else
            {
                type_mask = 0;

                if (token != PARSER_EXHAUSTED)
                {
                    log_unexpected();
                }
            }
            break;

        case PARSER_EXHAUSTED:
            type_mask = 0;
            break;

        default:
            type_mask = 0;
            log_unexpected();
        }

        return type_mask;
    }

    uint32_t parse_isolation_level(uint32_t type_mask)
    {
        token_t token = next_token(TOKEN_REQUIRED);

        switch (token)
        {
        case TOK_REPEATABLE:
            if (next_token(TOKEN_REQUIRED) != TOK_READ)
            {
                type_mask = 0;
                log_unexpected();
            }
            break;

        case TOK_READ:
            switch (next_token(TOKEN_REQUIRED))
            {
            case TOK_COMMITTED:
            case TOK_UNCOMMITTED:
                break;

            case PARSER_EXHAUSTED:
                type_mask = 0;
                break;

            default:
                type_mask = 0;
                log_unexpected();
                break;
            }
            break;

        case TOK_SERIALIZABLE:
            break;

        case PARSER_EXHAUSTED:
            type_mask = 0;
            break;

        default:
            type_mask = 0;
            log_unexpected();
            break;
        }

        return type_mask;
    }

    uint32_t parse_access_mode(uint32_t type_mask)
    {
        token_t token = next_token(TOKEN_REQUIRED);

        switch (token)
        {
        case TOK_WRITE:
        case TOK_ONLY:
            type_mask |= (token == TOK_WRITE ? mxs::sql::TYPE_READWRITE : mxs::sql::TYPE_READONLY);
            break;

        case PARSER_EXHAUSTED:
            type_mask = 0;
            break;

        default:
            type_mask = 0;
            log_unexpected();
            break;
        }

        return type_mask;
    }

    uint32_t parse_set_transaction(uint32_t type_mask)
    {
        token_t token = next_token(TOKEN_REQUIRED);

        switch (token)
        {
        case TOK_READ:
            type_mask = parse_access_mode(type_mask);

            if (next_token() == TOK_COMMA)
            {
                if (next_token(TOKEN_REQUIRED) == TOK_ISOLATION && next_token(TOKEN_REQUIRED) == TOK_LEVEL)
                {
                    type_mask = parse_isolation_level(type_mask);
                }
                else
                {
                    type_mask = 0;
                }
            }
            break;

        case TOK_ISOLATION:
            token = next_token(TOKEN_REQUIRED);

            if (token == TOK_LEVEL)
            {
                type_mask = parse_isolation_level(type_mask);

                if (next_token() == TOK_COMMA)
                {
                    if (next_token(TOKEN_REQUIRED) == TOK_READ)
                    {
                        type_mask = parse_access_mode(type_mask);
                    }
                    else
                    {
                        type_mask = 0;
                    }
                }
            }
            else
            {
                type_mask = 0;
            }
            break;

        case PARSER_EXHAUSTED:
            type_mask = 0;
            break;

        default:
            type_mask = 0;
            log_unexpected();
            break;
        }

        return type_mask;
    }

    uint32_t parse_set(uint32_t type_mask)
    {
        token_t token = next_token(TOKEN_REQUIRED);

        switch (token)
        {
        case TOK_AUTOCOMMIT:
            type_mask = parse_set_autocommit(type_mask);
            break;

        case TOK_SESSION:
            token = next_token(TOKEN_REQUIRED);
            if (token == TOK_AUTOCOMMIT)
            {
                type_mask = parse_set_autocommit(type_mask);
            }
            else if (token == TOK_TRANSACTION)
            {
                type_mask = parse_set_transaction(type_mask);
            }
            else
            {
                type_mask = 0;
                if (token != PARSER_EXHAUSTED)
                {
                    log_unexpected();
                }
            }
            break;

        case TOK_SESSION_VAR:
            token = next_token(TOKEN_REQUIRED);
            if (token == TOK_DOT)
            {
                token = next_token(TOKEN_REQUIRED);
                if (token == TOK_AUTOCOMMIT)
                {
                    type_mask = parse_set_autocommit(type_mask);
                }
                else
                {
                    type_mask = 0;
                    if (token != PARSER_EXHAUSTED)
                    {
                        log_unexpected();
                    }
                }
            }
            else
            {
                type_mask = 0;
                if (token != PARSER_EXHAUSTED)
                {
                    log_unexpected();
                }
            }
            break;

        case TOK_GLOBAL_VAR:
        case TOK_GLOBAL:
            // Modifications to global variables do not affect the current session.
            type_mask = 0;
            break;

        case TOK_TRANSACTION:
            type_mask |= mxs::sql::TYPE_NEXT_TRX;
            type_mask = parse_set_transaction(type_mask);
            break;

        case PARSER_EXHAUSTED:
            type_mask = 0;
            break;

        default:
            type_mask = 0;
            log_unexpected();
        }

        return type_mask;
    }

    uint32_t parse_start(uint32_t type_mask)
    {
        token_t token = next_token(TOKEN_REQUIRED);

        switch (token)
        {
        case TOK_TRANSACTION:
            type_mask = parse_transaction(type_mask);
            break;

        case PARSER_EXHAUSTED:
            type_mask = 0;
            break;

        default:
            type_mask = 0;
            log_unexpected();
        }

        return type_mask;
    }

    uint32_t parse_transaction(uint32_t type_mask)
    {
        type_mask |= mxs::sql::TYPE_BEGIN_TRX;

        token_t token = next_token();

        switch (token)
        {
        case TOK_READ:
            type_mask = parse_read(type_mask);
            break;

        case TOK_WITH:
            type_mask = parse_with_consistent_snapshot(type_mask);
            break;

        case PARSER_EXHAUSTED:
            break;

        default:
            type_mask = 0;
            log_unexpected();
        }

        return type_mask;
    }

    uint32_t parse_with_consistent_snapshot(uint32_t type_mask)
    {
        token_t token = next_token(TOKEN_REQUIRED);

        if (token == TOK_CONSISTENT)
        {
            token = next_token(TOKEN_REQUIRED);

            if (token == TOK_SNAPSHOT)
            {
                token = next_token();

                switch (token)
                {
                case TOK_COMMA:
                    type_mask = parse_transaction(type_mask);
                    break;

                case PARSER_EXHAUSTED:
                    break;

                default:
                    type_mask = 0;
                    log_unexpected();
                }
            }
        }

        return type_mask;
    }

    uint32_t parse_work(uint32_t type_mask)
    {
        token_t token = next_token();

        switch (token)
        {
        case PARSER_EXHAUSTED:
            break;

        default:
            type_mask = 0;
            log_unexpected();
        }

        return type_mask;
    }

    uint32_t parse_write(uint32_t type_mask)
    {
        type_mask |= mxs::sql::TYPE_WRITE;

        token_t token = next_token();

        switch (token)
        {
        case TOK_COMMA:
            type_mask = parse_transaction(type_mask);
            break;

        case PARSER_EXHAUSTED:
            break;

        default:
            type_mask = 0;
            log_unexpected();
        }

        return type_mask;
    }

    uint32_t parse_xa(uint32_t type_mask)
    {
        switch (next_token(TOKEN_REQUIRED))
        {
        case TOK_START:
        case TOK_BEGIN:
            type_mask |= mxs::sql::TYPE_BEGIN_TRX;
            break;

        case TOK_END:
            type_mask |= mxs::sql::TYPE_COMMIT;
            break;

        case PARSER_EXHAUSTED:
            break;

        default:
            type_mask = 0;
            log_unexpected();
            break;
        }

        return type_mask;
    }

    // Significantly faster than library version.
    static char toupper(char c)
    {
        return (c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c;
    }

    token_t expect_token(const char* zWord, int len, token_t token)
    {
        const char* pI = m_pI;
        const char* pEnd = zWord + len;

        while ((pI < m_pEnd) && (zWord < pEnd) && (toupper(*pI) == *zWord))
        {
            ++pI;
            ++zWord;
        }

        if (zWord == pEnd)
        {
            if ((pI == m_pEnd) || (!isalpha(*pI)))      // Handwritten isalpha not faster than library
                                                        // version.
            {
                m_pI = pI;
            }
            else
            {
                token = PARSER_UNKNOWN_TOKEN;
            }
        }
        else
        {
            token = PARSER_UNKNOWN_TOKEN;
        }

        return token;
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
                MXB_INFO("Non-space data found after semi-colon: '%.*s'.",
                         (int)(m_pEnd - m_pI),
                         m_pI);
            }

            token = PARSER_EXHAUSTED;
        }
        else
        {
            switch (*m_pI)
            {
            case '@':
                if (is_next_alpha('A', 2))
                {
                    token = expect_token(TBP_EXPECT_TOKEN("@@AUTOCOMMIT"), TOK_AUTOCOMMIT);
                }
                else if (is_next_alpha('S', 2))
                {
                    token = expect_token(TBP_EXPECT_TOKEN("@@SESSION"), TOK_SESSION_VAR);
                }
                else if (is_next_alpha('G', 2))
                {
                    token = expect_token(TBP_EXPECT_TOKEN("@@GLOBAL"), TOK_GLOBAL_VAR);
                }
                break;

            case 'a':
            case 'A':
                token = expect_token(TBP_EXPECT_TOKEN("AUTOCOMMIT"), TOK_AUTOCOMMIT);
                break;

            case 'b':
            case 'B':
                token = expect_token(TBP_EXPECT_TOKEN("BEGIN"), TOK_BEGIN);
                break;

            case ',':
                ++m_pI;
                token = TOK_COMMA;
                break;

            case 'c':
            case 'C':
                if (is_next_alpha('O'))
                {
                    if (is_next_alpha('M', 2))
                    {
                        token = expect_token(TBP_EXPECT_TOKEN("COMMITTED"), TOK_COMMITTED);

                        if (token == PARSER_UNKNOWN_TOKEN)
                        {
                            token = expect_token(TBP_EXPECT_TOKEN("COMMIT"), TOK_COMMIT);
                        }
                    }
                    else if (is_next_alpha('N', 2))
                    {
                        token = expect_token(TBP_EXPECT_TOKEN("CONSISTENT"), TOK_CONSISTENT);
                    }
                }
                break;

            case '.':
                ++m_pI;
                token = TOK_DOT;
                break;

            case '=':
                ++m_pI;
                token = TOK_EQ;
                break;

            case 'e':
            case 'E':
                token = expect_token(TBP_EXPECT_TOKEN("END"), TOK_END);
                break;

            case 'f':
            case 'F':
                token = expect_token(TBP_EXPECT_TOKEN("FALSE"), TOK_FALSE);
                break;

            case 'g':
            case 'G':
                token = expect_token(TBP_EXPECT_TOKEN("GLOBAL"), TOK_GLOBAL);
                break;

            case '1':
                {
                    char c;
                    if (!peek_next_char(&c) || !isdigit(c))
                    {
                        ++m_pI;
                        token = TOK_ONE;
                    }
                }
                break;

            case 'i':
            case 'I':
                token = expect_token(TBP_EXPECT_TOKEN("ISOLATION"), TOK_ISOLATION);
                break;

            case 'l':
            case 'L':
                token = expect_token(TBP_EXPECT_TOKEN("LEVEL"), TOK_LEVEL);
                break;

            case 'o':
            case 'O':
                if (is_next_alpha('F'))
                {
                    token = expect_token(TBP_EXPECT_TOKEN("OFF"), TOK_ZERO);
                }
                else if (is_next_alpha('N'))
                {
                    if (is_next_alpha('L', 2))
                    {
                        token = expect_token(TBP_EXPECT_TOKEN("ONLY"), TOK_ONLY);
                    }
                    else
                    {
                        token = expect_token(TBP_EXPECT_TOKEN("ON"), TOK_ONE);
                    }
                }
                break;

            case 'r':
            case 'R':
                if (is_next_alpha('E'))
                {
                    if (is_next_alpha('P', 2))
                    {
                        token = expect_token(TBP_EXPECT_TOKEN("REPEATABLE"), TOK_REPEATABLE);
                    }
                    else
                    {
                        token = expect_token(TBP_EXPECT_TOKEN("READ"), TOK_READ);
                    }
                }
                else if (is_next_alpha('O'))
                {
                    token = expect_token(TBP_EXPECT_TOKEN("ROLLBACK"), TOK_ROLLBACK);
                }
                break;

            case 's':
            case 'S':
                if (is_next_alpha('E'))
                {
                    if (is_next_alpha('S', 2))
                    {
                        token = expect_token(TBP_EXPECT_TOKEN("SESSION"), TOK_SESSION);
                    }
                    else if (is_next_alpha('R', 2))
                    {
                        token = expect_token(TBP_EXPECT_TOKEN("SERIALIZABLE"), TOK_SERIALIZABLE);
                    }
                    else
                    {
                        token = expect_token(TBP_EXPECT_TOKEN("SET"), TOK_SET);
                    }
                }
                else if (is_next_alpha('N'))
                {
                    token = expect_token(TBP_EXPECT_TOKEN("SNAPSHOT"), TOK_SNAPSHOT);
                }
                else if (is_next_alpha('T'))
                {
                    token = expect_token(TBP_EXPECT_TOKEN("START"), TOK_START);
                }
                break;

            case 't':
            case 'T':
                if (is_next_alpha('R'))
                {
                    if (is_next_alpha('A', 2))
                    {
                        token = expect_token(TBP_EXPECT_TOKEN("TRANSACTION"), TOK_TRANSACTION);
                    }
                    else if (is_next_alpha('U', 2))
                    {
                        token = expect_token(TBP_EXPECT_TOKEN("TRUE"), TOK_TRUE);
                    }
                }
                break;

            case 'u':
            case 'U':
                token = expect_token(TBP_EXPECT_TOKEN("UNCOMMITTED"), TOK_UNCOMMITTED);
                break;

            case 'w':
            case 'W':
                if (is_next_alpha('I'))
                {
                    token = expect_token(TBP_EXPECT_TOKEN("WITH"), TOK_WITH);
                }
                else if (is_next_alpha('O'))
                {
                    token = expect_token(TBP_EXPECT_TOKEN("WORK"), TOK_WORK);
                }
                else if (is_next_alpha('R'))
                {
                    token = expect_token(TBP_EXPECT_TOKEN("WRITE"), TOK_WRITE);
                }
                break;

            case 'x':
            case 'X':
                token = expect_token(TBP_EXPECT_TOKEN("XA"), TOK_XA);
                break;

            case '0':
                {
                    char c;
                    if (!peek_next_char(&c) || !isdigit(c))
                    {
                        ++m_pI;
                        token = TOK_ZERO;
                    }
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
