/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include <maxbase/assert.h>
#include <maxbase/string.hh>
#include <cstring>
#include <functional>
#include <limits>

namespace
{
const char IS_SPACE = 0x01;
const char IS_COMMENT = 0x02;

/** This LUT checks that a character can only have one classification
 *  which allows the bitmap to be used in a switch. Minimal class,
 *  but it would make sense to move it into maxbase. TODO.
 */
class LUT
{
public:
    LUT()
    {
        set(IS_SPACE, ::isspace);
        set(IS_COMMENT, [](uint8_t c) {
                return std::string("/#-").find(c) != std::string::npos;
            });
    }

    inline bool operator()(char bit, uint8_t c) const
    {
        return m_table[c] & bit;
    }

    // Return the bits for the character.
    inline char bitmap(uint8_t c)
    {
        return m_table[c];
    }

private:
    void set(char bit, std::function<bool(uint8_t)> is_type)
    {
        for (int i = 0; i <= std::numeric_limits<uint8_t>::max(); i++)
        {
            if (is_type(i))
            {
                mxb_assert(m_table[i] == 0);
                m_table[i] |= bit;
            }
        }
    }

    std::array<char, 256> m_table = {};
};

LUT lut;

// Return true if the string starts with case insensitive "USE", "KIL" or "SET"
inline bool has_special_prefix(const char* a, const char* pEnd)
{
    auto len = pEnd - a;
    const char* b = a + 1;
    const char* c = a + 2;
    bool match = (len >= 3)
        && ((((*a == 'u') || (*a == 'U')) && ((*b == 's') || (*b == 'S')) && ((*c == 'e') || (*c == 'E')))
            ||
            (((*a == 'k') || (*a == 'K')) && ((*b == 'i') || (*b == 'I')) && ((*c == 'l') || (*c == 'L')))
            ||
            (((*a == 's') || (*a == 'S')) && ((*b == 'e') || (*b == 'E')) && ((*c == 't') || (*c == 'T')))
            );

    return match;
}
}

bool detect_special_query(const char** ppSql, const char* pEnd)
{
    bool is_special = false;

    const char* pSql = *ppSql;
    while (pSql < pEnd)
    {
        switch (lut.bitmap(*pSql))
        {
        case IS_SPACE:
            ++pSql;
            break;

        case IS_COMMENT:
            {
                auto ptr_before = pSql;
                pSql = maxbase::consume_comment(pSql, pEnd);
                if (pSql == ptr_before)
                {
                    goto break_out;
                }
            }
            break;

        default:
            // whitespace and comments have been skipped
            is_special = has_special_prefix(pSql, pEnd);
            goto break_out;
        }
    }

break_out:

    if (is_special)
    {
        *ppSql = pSql;
    }

    return is_special;
}
