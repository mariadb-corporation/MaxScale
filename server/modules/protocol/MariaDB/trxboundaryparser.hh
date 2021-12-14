/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <ctype.h>
#include <maxscale/customparser.hh>
#include <maxscale/protocol/mariadb/query_classifier.hh>

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
        TK_AUTOCOMMIT,
        TK_BEGIN,
        TK_COMMA,
        TK_COMMIT,
        TK_COMMITTED,
        TK_CONSISTENT,
        TK_DOT,
        TK_END,
        TK_EQ,
        TK_FALSE,
        TK_GLOBAL,
        TK_GLOBAL_VAR,
        TK_ISOLATION,
        TK_LEVEL,
        TK_ONE,
        TK_ONLY,
        TK_READ,
        TK_REPEATABLE,
        TK_ROLLBACK,
        TK_SESSION,
        TK_SESSION_VAR,
        TK_SET,
        TK_SERIALIZABLE,
        TK_SNAPSHOT,
        TK_START,
        TK_TRANSACTION,
        TK_TRUE,
        TK_UNCOMMITTED,
        TK_WITH,
        TK_WORK,
        TK_WRITE,
        TK_XA,
        TK_ZERO,

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
     * @param pSql  SQL statament.
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
    uint32_t type_mask_of(GWBUF* pBuf)
    {
        uint32_t type_mask = 0;

        char* pSql;
        if (modutil_extract_SQL(pBuf, &pSql, &m_len))
        {
            m_pSql = pSql;
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
        MXS_NOTICE("Transaction tracking: In statement '%.*s', unexpected token at '%.*s'.",
                   (int)m_len,
                   m_pSql,
                   (int)(m_pEnd - m_pI),
                   m_pI);
#endif
    }

    void log_exhausted()
    {
#ifdef TBP_LOG_UNEXPECTED_AND_EXHAUSTED
        MXS_NOTICE("Transaction tracking: More tokens expected in statement '%.*s'.", (int)m_len, m_pSql);
#endif
    }

    uint32_t parse()
    {
        uint32_t type_mask = 0;

        token_t token = next_token();

        switch (token)
        {
        case TK_BEGIN:
            type_mask = parse_begin(type_mask);
            break;

        case TK_COMMIT:
            type_mask = parse_commit(type_mask);
            break;

        case TK_ROLLBACK:
            type_mask = parse_rollback(type_mask);
            break;

        case TK_START:
            type_mask = parse_start(type_mask);
            break;

        case TK_SET:
            type_mask = parse_set(0);
            break;

        case TK_XA:
            type_mask = parse_xa(type_mask);
            break;

        default:
            ;
        }

        return type_mask;
    }

    uint32_t parse_begin(uint32_t type_mask)
    {
        type_mask |= QUERY_TYPE_BEGIN_TRX;

        token_t token = next_token();

        switch (token)
        {
        case TK_WORK:
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
        type_mask |= QUERY_TYPE_COMMIT;

        token_t token = next_token();

        switch (token)
        {
        case TK_WORK:
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
        type_mask |= QUERY_TYPE_READ;

        token_t token = next_token();

        switch (token)
        {
        case TK_COMMA:
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
        case TK_ONLY:
            type_mask = parse_only(type_mask);
            break;

        case TK_WRITE:
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
        type_mask |= QUERY_TYPE_ROLLBACK;

        token_t token = next_token();

        switch (token)
        {
        case TK_WORK:
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
        case TK_EQ:
            token = next_token(TOKEN_REQUIRED);
            if (token == TK_ONE || token == TK_TRUE)
            {
                type_mask |= (QUERY_TYPE_COMMIT | QUERY_TYPE_ENABLE_AUTOCOMMIT);
            }
            else if (token == TK_ZERO || token == TK_FALSE)
            {
                type_mask = (QUERY_TYPE_BEGIN_TRX | QUERY_TYPE_DISABLE_AUTOCOMMIT);
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
        case TK_REPEATABLE:
            if (next_token(TOKEN_REQUIRED) != TK_READ)
            {
                type_mask = 0;
                log_unexpected();
            }
            break;

        case TK_READ:
            switch (next_token(TOKEN_REQUIRED))
            {
            case TK_COMMITTED:
            case TK_UNCOMMITTED:
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

        case TK_SERIALIZABLE:
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
        case TK_WRITE:
        case TK_ONLY:
            type_mask |= (token == TK_WRITE ? QUERY_TYPE_READWRITE : QUERY_TYPE_READONLY);
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
        case TK_READ:
            type_mask = parse_access_mode(type_mask);

            if (next_token() == TK_COMMA)
            {
                if (next_token(TOKEN_REQUIRED) == TK_ISOLATION && next_token(TOKEN_REQUIRED) == TK_LEVEL)
                {
                    type_mask = parse_isolation_level(type_mask);
                }
                else
                {
                    type_mask = 0;
                }
            }
            break;

        case TK_ISOLATION:
            token = next_token(TOKEN_REQUIRED);

            if (token == TK_LEVEL)
            {
                type_mask = parse_isolation_level(type_mask);

                if (next_token() == TK_COMMA)
                {
                    if (next_token(TOKEN_REQUIRED) == TK_READ)
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
        case TK_AUTOCOMMIT:
            type_mask = parse_set_autocommit(type_mask);
            break;

        case TK_SESSION:
            token = next_token(TOKEN_REQUIRED);
            if (token == TK_AUTOCOMMIT)
            {
                type_mask = parse_set_autocommit(type_mask);
            }
            else if (token == TK_TRANSACTION)
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

        case TK_SESSION_VAR:
            token = next_token(TOKEN_REQUIRED);
            if (token == TK_DOT)
            {
                token = next_token(TOKEN_REQUIRED);
                if (token == TK_AUTOCOMMIT)
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

        case TK_GLOBAL_VAR:
        case TK_GLOBAL:
            // Modifications to global variables do not affect the current session.
            type_mask = 0;
            break;

        case TK_TRANSACTION:
            type_mask |= QUERY_TYPE_NEXT_TRX;
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
        case TK_TRANSACTION:
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
        type_mask |= QUERY_TYPE_BEGIN_TRX;

        token_t token = next_token();

        switch (token)
        {
        case TK_READ:
            type_mask = parse_read(type_mask);
            break;

        case TK_WITH:
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

        if (token == TK_CONSISTENT)
        {
            token = next_token(TOKEN_REQUIRED);

            if (token == TK_SNAPSHOT)
            {
                token = next_token();

                switch (token)
                {
                case TK_COMMA:
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
        type_mask |= QUERY_TYPE_WRITE;

        token_t token = next_token();

        switch (token)
        {
        case TK_COMMA:
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
        case TK_START:
        case TK_BEGIN:
            type_mask |= QUERY_TYPE_BEGIN_TRX;
            break;

        case TK_END:
            type_mask |= QUERY_TYPE_COMMIT;
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

    void bypass_whitespace()
    {
        m_pI = modutil_MySQL_bypass_whitespace(const_cast<char*>(m_pI), m_pEnd - m_pI);
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
                MXS_INFO("Non-space data found after semi-colon: '%.*s'.",
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
                    token = expect_token(TBP_EXPECT_TOKEN("@@AUTOCOMMIT"), TK_AUTOCOMMIT);
                }
                else if (is_next_alpha('S', 2))
                {
                    token = expect_token(TBP_EXPECT_TOKEN("@@SESSION"), TK_SESSION_VAR);
                }
                else if (is_next_alpha('G', 2))
                {
                    token = expect_token(TBP_EXPECT_TOKEN("@@GLOBAL"), TK_GLOBAL_VAR);
                }
                break;

            case 'a':
            case 'A':
                token = expect_token(TBP_EXPECT_TOKEN("AUTOCOMMIT"), TK_AUTOCOMMIT);
                break;

            case 'b':
            case 'B':
                token = expect_token(TBP_EXPECT_TOKEN("BEGIN"), TK_BEGIN);
                break;

            case ',':
                ++m_pI;
                token = TK_COMMA;
                break;

            case 'c':
            case 'C':
                if (is_next_alpha('O'))
                {
                    if (is_next_alpha('M', 2))
                    {
                        token = expect_token(TBP_EXPECT_TOKEN("COMMITTED"), TK_COMMITTED);

                        if (token == PARSER_UNKNOWN_TOKEN)
                        {
                            token = expect_token(TBP_EXPECT_TOKEN("COMMIT"), TK_COMMIT);
                        }
                    }
                    else if (is_next_alpha('N', 2))
                    {
                        token = expect_token(TBP_EXPECT_TOKEN("CONSISTENT"), TK_CONSISTENT);
                    }
                }
                break;

            case '.':
                ++m_pI;
                token = TK_DOT;
                break;

            case '=':
                ++m_pI;
                token = TK_EQ;
                break;

            case 'e':
            case 'E':
                token = expect_token(TBP_EXPECT_TOKEN("END"), TK_END);
                break;

            case 'f':
            case 'F':
                token = expect_token(TBP_EXPECT_TOKEN("FALSE"), TK_FALSE);
                break;

            case 'g':
            case 'G':
                token = expect_token(TBP_EXPECT_TOKEN("GLOBAL"), TK_GLOBAL);
                break;

            case '1':
                {
                    char c;
                    if (!peek_next_char(&c) || !isdigit(c))
                    {
                        ++m_pI;
                        token = TK_ONE;
                    }
                }
                break;

            case 'i':
            case 'I':
                token = expect_token(TBP_EXPECT_TOKEN("ISOLATION"), TK_ISOLATION);
                break;

            case 'l':
            case 'L':
                token = expect_token(TBP_EXPECT_TOKEN("LEVEL"), TK_LEVEL);
                break;

            case 'o':
            case 'O':
                if (is_next_alpha('F'))
                {
                    token = expect_token(TBP_EXPECT_TOKEN("OFF"), TK_ZERO);
                }
                else if (is_next_alpha('N'))
                {
                    if (is_next_alpha('L', 2))
                    {
                        token = expect_token(TBP_EXPECT_TOKEN("ONLY"), TK_ONLY);
                    }
                    else
                    {
                        token = expect_token(TBP_EXPECT_TOKEN("ON"), TK_ONE);
                    }
                }
                break;

            case 'r':
            case 'R':
                if (is_next_alpha('E'))
                {
                    if (is_next_alpha('P', 2))
                    {
                        token = expect_token(TBP_EXPECT_TOKEN("REPEATABLE"), TK_REPEATABLE);
                    }
                    else
                    {
                        token = expect_token(TBP_EXPECT_TOKEN("READ"), TK_READ);
                    }
                }
                else if (is_next_alpha('O'))
                {
                    token = expect_token(TBP_EXPECT_TOKEN("ROLLBACK"), TK_ROLLBACK);
                }
                break;

            case 's':
            case 'S':
                if (is_next_alpha('E'))
                {
                    if (is_next_alpha('S', 2))
                    {
                        token = expect_token(TBP_EXPECT_TOKEN("SESSION"), TK_SESSION);
                    }
                    else if (is_next_alpha('R', 2))
                    {
                        token = expect_token(TBP_EXPECT_TOKEN("SERIALIZABLE"), TK_SERIALIZABLE);
                    }
                    else
                    {
                        token = expect_token(TBP_EXPECT_TOKEN("SET"), TK_SET);
                    }
                }
                else if (is_next_alpha('N'))
                {
                    token = expect_token(TBP_EXPECT_TOKEN("SNAPSHOT"), TK_SNAPSHOT);
                }
                else if (is_next_alpha('T'))
                {
                    token = expect_token(TBP_EXPECT_TOKEN("START"), TK_START);
                }
                break;

            case 't':
            case 'T':
                if (is_next_alpha('R'))
                {
                    if (is_next_alpha('A', 2))
                    {
                        token = expect_token(TBP_EXPECT_TOKEN("TRANSACTION"), TK_TRANSACTION);
                    }
                    else if (is_next_alpha('U', 2))
                    {
                        token = expect_token(TBP_EXPECT_TOKEN("TRUE"), TK_TRUE);
                    }
                }
                break;

            case 'u':
            case 'U':
                token = expect_token(TBP_EXPECT_TOKEN("UNCOMMITTED"), TK_UNCOMMITTED);
                break;

            case 'w':
            case 'W':
                if (is_next_alpha('I'))
                {
                    token = expect_token(TBP_EXPECT_TOKEN("WITH"), TK_WITH);
                }
                else if (is_next_alpha('O'))
                {
                    token = expect_token(TBP_EXPECT_TOKEN("WORK"), TK_WORK);
                }
                else if (is_next_alpha('R'))
                {
                    token = expect_token(TBP_EXPECT_TOKEN("WRITE"), TK_WRITE);
                }
                break;

            case 'x':
            case 'X':
                token = expect_token(TBP_EXPECT_TOKEN("XA"), TK_XA);
                break;

            case '0':
                {
                    char c;
                    if (!peek_next_char(&c) || !isdigit(c))
                    {
                        ++m_pI;
                        token = TK_ZERO;
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
