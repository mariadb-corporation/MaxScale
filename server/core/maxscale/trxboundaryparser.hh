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
#include <maxscale/query_classifier.h>

namespace maxscale
{

#define TBP_EXPECT_TOKEN(string_literal) string_literal, (sizeof(string_literal) - 1)

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
class TrxBoundaryParser
{
public:
    enum token_t
    {
        TK_AUTOCOMMIT,
        TK_BEGIN,
        TK_COMMIT,
        TK_EQ,
        TK_FALSE,
        TK_ONE,
        TK_ONLY,
        TK_READ,
        TK_ROLLBACK,
        TK_SET,
        TK_START,
        TK_TRANSACTION,
        TK_TRUE,
        TK_WORK,
        TK_WRITE,
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
        : m_pSql(NULL)
        , m_len(0)
        , m_pI(NULL)
        , m_pEnd(NULL)
    {
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

        if (modutil_extract_SQL(pBuf, &m_pSql, &m_len))
        {
            m_pI = m_pSql;
            m_pEnd = m_pI + m_len;

            type_mask = parse();
        }

        return type_mask;
    }

private:
    void log_unexpected()
    {
        MXS_ERROR("In statement '%.*s', unexpected token at '%.*s'.",
                  (int)m_len, m_pSql, (int)(m_pEnd - m_pI), m_pI);
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
        token_t token = next_token();

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
            MXS_WARNING("Expected more.");
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

    uint32_t parse_set(uint32_t type_mask)
    {
        token_t token = next_token();

        switch (token)
        {
        case TK_AUTOCOMMIT:
            token = next_token();
            if (token == TK_EQ)
            {
                token = next_token();
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
                    log_unexpected();
                }
            }
            else
            {
                type_mask = 0;
                MXS_WARNING("Expected more.");
            }
            break;

        case PARSER_EXHAUSTED:
            type_mask = 0;
            MXS_WARNING("Expected more.");
            break;

        default:
            type_mask = 0;
            log_unexpected();
        }

        return type_mask;
    }

    uint32_t parse_start(uint32_t type_mask)
    {
        token_t token = next_token();

        switch (token)
        {
        case TK_TRANSACTION:
            type_mask = parse_transaction(type_mask);
            break;

        case PARSER_EXHAUSTED:
            type_mask = 0;
            MXS_WARNING("Expected more.");
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

        case PARSER_EXHAUSTED:
            break;

        default:
            MXS_WARNING("START TRANSACTION followed by '%.*s', assuming it's ok.",
                        (int)(m_pEnd - m_pI), m_pI);
            //type_mask = 0;
            log_unexpected();
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
        case PARSER_EXHAUSTED:
            break;

        default:
            type_mask = 0;
            log_unexpected();
        }

        return type_mask;
    }

    bool is_next_char(char c, int offset = 1) const
    {
        return ((m_pI + offset) < m_pEnd) && (*(m_pI + offset) == c);
    }

    bool get_next_char(char* pC) const
    {
        bool rc = (m_pI + 1 < m_pEnd);

        if (rc)
        {
            *pC = *(m_pI + 1);
        }

        return rc;
    }

    token_t expect_token(const char* zWord, int len, token_t token)
    {
        const char* pNext = m_pI;

        while ((pNext < m_pEnd) && isalpha(*pNext))
        {
            ++pNext;
        }

        if (pNext - m_pI == len)
        {
            if (strncasecmp(m_pI, zWord, len) == 0)
            {
                m_pI = pNext;
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

    token_t next_token()
    {
        token_t token = PARSER_UNKNOWN_TOKEN;

        while ((m_pI != m_pEnd) && isblank(*m_pI))
        {
            ++m_pI;
        }

        if (m_pI == m_pEnd)
        {
            token = PARSER_EXHAUSTED;
        }
        else if (*m_pI == ';')
        {
            ++m_pI;

            while ((m_pI != m_pEnd) && isblank(*m_pI))
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
            case 'a':
            case 'A':
                token = expect_token(TBP_EXPECT_TOKEN("autocommit"), TK_AUTOCOMMIT);
                break;

            case 'b':
            case 'B':
                token = expect_token(TBP_EXPECT_TOKEN("begin"), TK_BEGIN);
                break;

            case 'c':
            case 'C':
                token = expect_token(TBP_EXPECT_TOKEN("commit"), TK_COMMIT);
                break;

            case '=':
                if (!is_next_char('='))
                {
                    ++m_pI;
                    token = TK_EQ;
                }
                break;

            case 'f':
            case 'F':
                token = expect_token(TBP_EXPECT_TOKEN("false"), TK_FALSE);
                break;

            case '1':
                {
                    char c;
                    if (!get_next_char(&c) || !isdigit(c))
                    {
                        ++m_pI;
                        token = TK_ONE;
                    }
                }
                break;

            case 'o':
            case 'O':
                token = expect_token(TBP_EXPECT_TOKEN("only"), TK_ONLY);
                break;

            case 'r':
            case 'R':
                if (is_next_char('e') || is_next_char('E'))
                {
                    token = expect_token(TBP_EXPECT_TOKEN("read"), TK_READ);
                }
                else if (is_next_char('o') || is_next_char('O'))
                {
                    token = expect_token(TBP_EXPECT_TOKEN("rollback"), TK_ROLLBACK);
                }
                break;

            case 's':
            case 'S':
                if (is_next_char('e') || is_next_char('E'))
                {
                    token = expect_token(TBP_EXPECT_TOKEN("set"), TK_SET);
                }
                else if (is_next_char('t') || is_next_char('T'))
                {
                    token = expect_token(TBP_EXPECT_TOKEN("start"), TK_START);
                }
                break;

            case 't':
            case 'T':
                if (is_next_char('r') || is_next_char('R'))
                {
                    if (is_next_char('a', 2) || is_next_char('A', 2))
                    {
                        token = expect_token(TBP_EXPECT_TOKEN("transaction"), TK_TRANSACTION);
                    }
                    else if (is_next_char('u', 2) || is_next_char('U', 2))
                    {
                        token = expect_token(TBP_EXPECT_TOKEN("true"), TK_TRUE);
                    }
                }
                break;

            case 'w':
            case 'W':
                if (is_next_char('o') || is_next_char('O'))
                {
                    token = expect_token(TBP_EXPECT_TOKEN("work"), TK_WORK);
                }
                else if (is_next_char('r') || is_next_char('R'))
                {
                    token = expect_token(TBP_EXPECT_TOKEN("write"), TK_WRITE);
                }
                break;

            case '0':
                {
                    char c;
                    if (!get_next_char(&c) || !isdigit(c))
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

        return token;
    }

private:
    TrxBoundaryParser(const TrxBoundaryParser&);
    TrxBoundaryParser& operator = (const TrxBoundaryParser&);

private:
    char*       m_pSql;
    int         m_len;
    const char* m_pI;
    const char* m_pEnd;
};

}
