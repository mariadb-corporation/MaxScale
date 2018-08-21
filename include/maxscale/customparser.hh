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
#include <maxscale/log.h>
#include <maxscale/modutil.h>
#include <ctype.h>

namespace maxscale
{

#define MXS_CP_EXPECT_TOKEN(string_literal) string_literal, (sizeof(string_literal) - 1)

// For debugging purposes.
// #define MXS_CP_LOG_UNEXPECTED_AND_EXHAUSTED
#undef MXS_CP_LOG_UNEXPECTED_AND_EXHAUSTED

class CustomParser
{
    CustomParser(const CustomParser&);
    CustomParser& operator = (const CustomParser&);

public:
    typedef int32_t token_t;

    enum token_required_t
    {
        TOKEN_REQUIRED,
        TOKEN_NOT_REQUIRED,
    };

    enum
    {
        PARSER_UNKNOWN_TOKEN = -2,
        PARSER_EXHAUSTED     = -1
    };

    CustomParser()
        : m_pSql(NULL)
        , m_len(0)
        , m_pI(NULL)
        , m_pEnd(NULL)
    {
    }

protected:
    /**
     * To be called when unexpected data is encountered. For debugging
     * purposes, logging will only be performed if the define
     * MXS_CP_LOG_UNEXPECTED_AND_EXHAUSTED is defined.
     */
    void log_unexpected()
    {
#ifdef MXS_CP_LOG_UNEXPECTED_AND_EXHAUSTED
        MXS_NOTICE("Custom parser: In statement '%.*s', unexpected token at '%.*s'.",
                   (int)m_len, m_pSql, (int)(m_pEnd - m_pI), m_pI);
#endif
    }

    /**
     * To be called when there is no more data even though there is
     * expected to be. For debugging purposes, logging will only be
     * performed if the define MXS_CP_LOG_UNEXPECTED_AND_EXHAUSTED
     * is defined.
     */
    void log_exhausted()
    {
#ifdef MXS_CP_LOG_UNEXPECTED_AND_EXHAUSTED
        MXS_NOTICE("Custom parser: More tokens expected in statement '%.*s'.", (int)m_len, m_pSql);
#endif
    }

    /**
     * Is the character an alphabetic character.
     *
     * @param c A char
     *
     * @return True if @c c is between 'a' and 'z' or 'A' and 'Z', inclusive.
     */
    static bool is_alpha(char c)
    {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    }

    /**
     * Is the character a number
     *
     * @param c A char
     *
     * @return True if @c c is between '0' and '9' inclusive.
     */
    static bool is_number(char c)
    {
        return (c >= '0' && c <= '9');
    }

    /**
     * Is a character some offset from the current position, a specific one.
     *
     * @param uc      An UPPERCASE character.
     * @param offset  How many characters from the current position.
     *
     * @return True if the character at the position is the one specified or
     *         its lowercase equivalent.
     */
    bool is_next_alpha(char uc, int offset = 1) const
    {
        ss_dassert(uc >= 'A' && uc <= 'Z');

        char lc = uc + ('a' - 'A');

        return
            ((m_pI + offset) < m_pEnd) &&
            ((*(m_pI + offset) == uc) || (*(m_pI + offset) == lc));
    }

    /**
     * Peek current character.
     *
     * @param pC  Upon successful return will be the current character.
     *
     * @return True, if the current character was returned, false otherwise.
     *         False will only be returned if the current position is at
     *         the end.
     */
    bool peek_current_char(char* pC) const
    {
        if (m_pI != m_pEnd)
        {
            *pC = *m_pI;
        }

        return m_pI != m_pEnd;
    }

    /**
     * Peek next character.
     *
     * @param pC  Upon successful return will be the next character.
     *
     * @return True, if the next character was returned, false otherwise.
     *         False will only be returned if the current position is at
     *         the end.
     */
    bool peek_next_char(char* pC) const
    {
        bool rc = (m_pI + 1 < m_pEnd);

        if (rc)
        {
            *pC = *(m_pI + 1);
        }

        return rc;
    }

    /**
     * Convert a character to upper case.
     *
     * @param c The character to convert.
     *
     * @return The uppercase equivalent. If @c c is already uppercase,
     *         then it is returned.
     */
    static char toupper(char c)
    {
        // Significantly faster than library version.
        return (c >= 'a' && c <='z') ? c - ('a' - 'A') : c;
    }

    /**
     * Bypass all whitespace from current position.
     */
    void bypass_whitespace()
    {
        m_pI = modutil_MySQL_bypass_whitespace(const_cast<char*>(m_pI), m_pEnd - m_pI);
    }

    /**
     * Check whether an expected token is available.
     *
     * @param zWord  A token.
     * @param len    The token length.
     * @param token  The value to be returned if the next token is the
     *               expected one.
     *
     * @return @c token if the current token is the expected one,
     *         otherwise PARSER_UNKNOWN_TOKEN.
     */
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
            if ((pI == m_pEnd) || (!isalpha(*pI))) // Handwritten isalpha not faster than library version.
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

protected:
    const char* m_pSql;
    int         m_len;
    const char* m_pI;
    const char* m_pEnd;
};

}
