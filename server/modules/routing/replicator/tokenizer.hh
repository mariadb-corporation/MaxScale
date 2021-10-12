/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tok
{

using Sanitizer = std::function<std::string (const char*, int)>;

static std::string default_sanitizer(const char* sql, int len)
{
    return std::string(sql, len);
}

enum Type
{
    // Used for non-keyword tokens e.g. string literals and identifiers
    ID,

    // SQL keywords
    ADD,
    AFTER,
    ALTER,
    AUTO_INCREMENT,
    CASCADE,
    CHANGE,
    COLUMN_FORMAT,
    COLUMN,
    COMMENT,
    CONSTRAINT,
    CREATE,
    DEFAULT,
    DROP,
    DYNAMIC,
    EXISTS,
    FIRST,
    FIXED,
    FOREIGN,
    FULLTEXT,
    IF,
    IGNORE,
    INDEX,
    INVISIBLE,
    KEY,
    KEYS,
    LIKE,
    MODIFY,
    NOT,
    ONLINE,
    OR,
    PERIOD,
    PRIMARY,
    REF_SYSTEM_ID,
    REMOVE,
    RENAME,
    REPLACE,
    RESTRICT,
    SPATIAL,
    SYSTEM,
    TABLE,
    TO,
    UNIQUE,
    UNSIGNED,
    VERSIONING,
    WITH,
    WITHOUT,
    ZEROFILL,

    // Special characters
    DOT,
    COMMA,
    LP,
    RP,
    EQ,
    SQLNULL,

    // Marks the end of the token list. Returned by Tokenizer::Chain when tokenizer is exhausted to remove the
    // need for bounds checks.
    EXHAUSTED
};

class Tokenizer
{
public:

    struct Token
    {
        Token(Type t, const char* s)
            : m_type(t)
            , m_str(s)
        {
        }

        Token(Type t, const char* s, int l, Sanitizer sanitizer)
            : m_type(t)
            , m_str(s)
            , m_len(l)
            , m_sanitizer(sanitizer)
        {
        }

        Token(Type t)
            : m_type(t)
        {
        }

        Token()
            : m_type(EXHAUSTED)
        {
        }

        static std::string to_string(Type t)
        {
            return Token(t).to_string();
        }

        std::string to_string() const;

        Type type() const
        {
            return m_type;
        }

        const char* position() const
        {
            return m_str;
        }

        std::string value() const;

    private:
        Type        m_type;
        const char* m_str {nullptr};
        int         m_len {0};
        Sanitizer   m_sanitizer {default_sanitizer};
    };

    class Chain
    {
    public:
        using Container = std::deque<Token>;

        Token chomp()
        {
            Token rv;

            if (!m_tokens.empty())
            {
                rv = std::move(m_tokens.front());
                m_tokens.pop_front();
            }

            return rv;
        }

        Token front() const
        {
            Token rv;

            if (!m_tokens.empty())
            {
                rv = m_tokens.front();
            }

            return rv;
        }

        Container::iterator begin()
        {
            return m_tokens.begin();
        }

        Container::iterator end()
        {
            return m_tokens.end();
        }

    private:
        friend class Tokenizer;
        Container m_tokens;
    };

    static Chain tokenize(const char* sql, Sanitizer sanitizer = default_sanitizer);

private:
    static const std::unordered_map<std::string, Type> s_tokens;
};

bool operator==(const Tokenizer::Token& lhs, const Tokenizer::Token& rhs);
}
