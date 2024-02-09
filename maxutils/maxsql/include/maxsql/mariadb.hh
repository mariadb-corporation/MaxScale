/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxsql/ccdefs.hh>
#include <time.h>
#include <string.h>
#include <string>
#include <iostream>
#include <mysql.h>
#include <maxbase/assert.hh>

namespace maxsql
{
/**
 * Execute a query, manually defining retry limits.
 *
 * @param conn MySQL connection
 * @param query Query to execute
 * @param query_retries Maximum number of retries
 * @param query_retry_timeout Maximum time to spend retrying, in seconds
 * @return return value of mysql_query
 */
int mysql_query_ex(MYSQL* conn, const std::string& query, int query_retries, time_t query_retry_timeout);

/**
 * Check if the MYSQL error number is a connection error.
 *
 * @param Error code
 * @return True if the MYSQL error number is a connection error
 */
bool mysql_is_net_error(unsigned int errcode);

/**
 * Enable/disable the logging of all SQL statements MaxScale sends to
 * the servers.
 *
 * @param enable If true, enable, if false, disable.
 */
void mysql_set_log_statements(bool enable);

/**
 * Returns whether SQL statements sent to the servers are logged or not.
 *
 * @return True, if statements are logged, false otherwise.
 */
bool mysql_get_log_statements();

/** Length-encoded integers */
size_t   leint_bytes(const uint8_t* ptr);
uint64_t leint_value(const uint8_t* c);
uint64_t leint_consume(uint8_t** c);

/** Length-encoded strings */
char*       lestr_consume_dup(uint8_t** c);
char*       lestr_consume(uint8_t** c, size_t* size);
const char* lestr_consume_safe(const uint8_t** c, const uint8_t* end, size_t* size);

// Logs the statement if statement logging is enabled
void log_statement(int rc, MYSQL* conn, const std::string& query);

/**
 * Get server capabilities
 *
 * @param conn Connection to use
 *
 * @return The 64 bits of capabilities. The lower 32 bits are the basic capabilities and the upper 32 bits are
 * the MariaDB extended ones.
 */
uint64_t mysql_get_server_capabilities(MYSQL* conn);

/**
 * Causes a PROXY UNKNOWN header to be sent when the connection is created
 *
 * If a server has proxy protocol enabled, internal connections to it should construct a valid proxy protocol
 * header. A valid header cannot be created with connector-c as the source address and port are unknown at the
 * time of creation. To still comply with the specification, a PROXY UNKNOWN header can be used.
 *
 * @param conn The connection to use
 */
void set_proxy_header(MYSQL* conn);


/**
 * @class LEncInt
 *
 * @c LEncInt is a thin wrapper around a MySQL length encoded integer
 * that makes it simple to extract length encoded integers from packets.
 */
class LEncInt
{
public:
    /**
     * Constructor
     *
     * @param pData  Pointer to the beginning of an length encoded integer.
     */
    LEncInt(uint8_t* pData)
    {
        m_value = leint_value(pData);
    }

    /**
     * Constructor
     *
     * @param pData  Pointer to a pointer to the beginning of an length
     *               encoded integer. After the call, the pointer will be advanced
     *               to point at the byte following the length encoded integer.
     */
    LEncInt(uint8_t** ppData)
    {
        size_t nBytes = leint_bytes(*ppData);
        m_value = leint_value(*ppData);
        *ppData += nBytes;
    }

    /**
     * @return The value of the length encoded integer.
     */
    uint64_t value() const
    {
        return m_value;
    }

    /**
     * @return The value of the length encoded integer.
     */
    operator uint64_t() const
    {
        return value();
    }

    /**
     * Write the integer to an @c std::ostream.
     *
     * @param out  The stream.
     *
     * @return The stream provided as argument.
     */
    std::ostream& print(std::ostream& out) const
    {
        out << m_value;
        return out;
    }

private:
    uint64_t m_value;
};

/**
 * Stream the integer to an @c std::ostream.
 *
 * @param out  A stream.
 * @param i    A length encoded integer.
 *
 * @return The stream provided as argument.
 */
inline std::ostream& operator<<(std::ostream& out, const LEncInt& i)
{
    return i.print(out);
}

/**
 * @class LEncString
 *
 * @c LEncString is a thin wrapper around a MySQL length encoded string
 * that makes it simpler to use length encoded strings in conjunction with
 * @c char* and @c std::string strings.
 */
class LEncString
{
public:
    /**
     * @class iterator
     *
     * A _random access iterator_ to a @c LEncString.
     */
    class iterator
    {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = char;
        using difference_type = std::ptrdiff_t;
        using pointer = char*;
        using reference = char&;

        iterator(char* pS)
            : m_pS(pS)
        {
        }

        iterator& operator++()
        {
            mxb_assert(m_pS);
            ++m_pS;
            return *this;
        }

        iterator operator++(int)
        {
            iterator rv(*this);
            ++(*this);
            return rv;
        }

        iterator operator+(ptrdiff_t n)
        {
            mxb_assert(m_pS);
            iterator rv = m_pS;
            rv += n;
            return rv;
        }

        iterator& operator+=(ptrdiff_t n)
        {
            mxb_assert(m_pS);
            m_pS += n;
            return *this;
        }

        iterator& operator-=(ptrdiff_t n)
        {
            mxb_assert(m_pS);
            m_pS -= n;
            return *this;
        }

        ptrdiff_t operator-(const iterator& rhs) const
        {
            mxb_assert(m_pS);
            mxb_assert(rhs.m_pS);
            return m_pS - rhs.m_pS;
        }

        bool operator==(const iterator& rhs) const
        {
            return m_pS == rhs.m_pS;
        }

        bool operator!=(const iterator& rhs) const
        {
            return !(*this == rhs);
        }

        bool operator<(const iterator& rhs) const
        {
            return m_pS < rhs.m_pS;
        }

        bool operator<=(const iterator& rhs) const
        {
            return m_pS < rhs.m_pS;
        }

        bool operator>(const iterator& rhs) const
        {
            return m_pS > rhs.m_pS;
        }

        bool operator>=(const iterator& rhs) const
        {
            return m_pS > rhs.m_pS;
        }

        reference operator*()
        {
            mxb_assert(m_pS);
            return *m_pS;
        }

        reference operator[](ptrdiff_t i)
        {
            mxb_assert(m_pS);
            return m_pS[i];
        }

    private:
        char* m_pS;
    };

    /**
     * Constructor
     *
     * @param pData  Pointer to the beginning of a length encoded string
     */
    LEncString(uint8_t* pData, size_t length = std::numeric_limits<size_t>::max())
    {
        // NULL is sent as 0xfb. See https://dev.mysql.com/doc/internals/en/com-query-response.html
        if (length != 0 && *pData != 0xfb)
        {
            m_pString = lestr_consume(&pData, &m_length);
            mxb_assert(m_length <= length);
        }
        else
        {
            m_pString = NULL;
            m_length = 0;
        }
    }

    /**
     * Constructor
     *
     * @param ppData  Pointer to a pointer to the beginning of a length
     *                encoded string. After the call, the pointer will point
     *                one past the end of the length encoded string.
     */
    LEncString(uint8_t** ppData, size_t length = std::numeric_limits<size_t>::max())
    {
        // NULL is sent as 0xfb. See https://dev.mysql.com/doc/internals/en/com-query-response.html
        if (length != 0 && **ppData != 0xfb)
        {
            m_pString = lestr_consume(ppData, &m_length);
            mxb_assert(m_length <= length);
        }
        else
        {
            m_pString = NULL;
            m_length = 0;

            if (length != 0)
            {
                ++(*ppData);
            }
        }
    }

    /**
     * Returns an iterator to the beginning of the string.
     *
     * @return A random access iterator.
     */
    iterator begin()
    {
        return iterator(m_pString);
    }

    /**
     * Returns an iterator one past the end of the string.
     *
     * @return A random access iterator.
     */
    iterator end()
    {
        return iterator(m_pString + m_length);
    }

    /**
     * @return The length of the string.
     */
    size_t length() const
    {
        return m_length;
    }

    /**
     * @return True if the string is empty, false otherwise.
     */
    bool empty() const
    {
        return m_length == 0;
    }

    /**
     * Compare for equality in a case-sensitive fashion.
     *
     * @param s  The string to compare with.
     *
     * @return True, if the strings are equal.
     */
    bool eq(const LEncString& s) const
    {
        return m_length == s.m_length ? (memcmp(m_pString, s.m_pString, m_length) == 0) : false;
    }

    /**
     * Compare for equality in case-insensitive fashion.
     *
     * @param s  The string to compare with.
     *
     * @return True, if the strings are equal.
     */
    bool case_eq(const LEncString& s) const
    {
        return m_length == s.m_length ? (strncasecmp(m_pString, s.m_pString, m_length) == 0) : false;
    }

    /**
     * Compare for equality in a case-sensitive fashion
     *
     * @param s  The string to compare with.
     *
     * @return True, if the strings are equal.
     */
    bool eq(const char* zString) const
    {
        size_t length = strlen(zString);

        return m_length == length ? (memcmp(m_pString, zString, m_length) == 0) : false;
    }

    /**
     * Compare for equality in a case-insensitive fashion.
     *
     * @param s  The string to compare with.
     *
     * @return True, if the strings are equal.
     */
    bool case_eq(const char* zString) const
    {
        size_t length = strlen(zString);

        return m_length == length ? (strncasecmp(m_pString, zString, m_length) == 0) : false;
    }

    /**
     * Compare for equality in a case-sensitive fashion
     *
     * @param s  The string to compare with.
     *
     * @return True, if the strings are equal.
     */
    bool eq(const std::string& s) const
    {
        return m_length == s.length() ? (memcmp(m_pString, s.data(), m_length) == 0) : false;
    }

    /**
     * Compare for equality in a case-insensitive fashion
     *
     * @param s  The string to compare with.
     *
     * @return True, if the strings are equal.
     */
    bool case_eq(const std::string& s) const
    {
        return m_length == s.length() ? (strncasecmp(m_pString, s.data(), m_length) == 0) : false;
    }

    /**
     * Convert a @c LEncString to the equivalent @c std::string.
     *
     * @return An @c std::string
     */
    std::string to_string() const
    {
        if (m_pString)
        {
            return std::string(m_pString, m_length);
        }
        else
        {
            return std::string("NULL");
        }
    }

    /**
     * Print the string to a @c ostream.
     *
     * @param o  The @c ostream to print the string to.
     *
     * @return The stream provided as parameter.
     */
    std::ostream& print(std::ostream& o) const
    {
        o.write(m_pString, m_length);
        return o;
    }

    /**
     * Is NULL
     *
     * @return True, if the string represents a NULL value.
     */
    bool is_null() const
    {
        return m_pString == NULL;
    }

private:
    char*  m_pString;   /*<! Pointer to beginning of string, NOT zero-terminated. */
    size_t m_length;    /*<! Length of string. */
};

/**
 * Compare two strings for equality.
 *
 * @param lhs  A string.
 * @param rhs  Another string.
 *
 * @return True, if the strings are equal.
 */
inline bool operator==(const LEncString& lhs, const LEncString& rhs)
{
    return lhs.eq(rhs);
}

/**
 * Compare two strings for equality.
 *
 * @param lhs  A string.
 * @param rhs  Another string.
 *
 * @return True, if the strings are equal.
 */
inline bool operator==(const std::string& lhs, const LEncString& rhs)
{
    return rhs.eq(lhs);
}

/**
 * Compare two strings for equality.
 *
 * @param lhs  A string.
 * @param rhs  Another string.
 *
 * @return True, if the strings are equal.
 */
inline bool operator==(const LEncString& lhs, const std::string& rhs)
{
    return lhs.eq(rhs);
}

/**
 * Compare two strings for equality.
 *
 * @param lhs  A string.
 * @param rhs  Another string.
 *
 * @return True, if the strings are equal.
 */
inline bool operator==(const LEncString& lhs, const char* zRhs)
{
    return lhs.eq(zRhs);
}

/**
 * Stream a @c LEncString to an @c ostream.
 *
 * @param out  The @c ostream to stream to.
 * @param x    The string.
 *
 * @return The @c ostream provided as argument.
 */
inline std::ostream& operator<<(std::ostream& out, const LEncString& s)
{
    return s.print(out);
}

/**
 * Create an iterator placed at a particular position relative to another iterator.
 *
 * @param it  An iterator.
 * @param n   Steps to move, either negative or positive.
 *
 * @return An iterator referring to the new position. The result is undefined
 *         if @c n causes the iterator to conceptually move before the beginning
 *         of the string or beyond the end.
 */
inline LEncString::iterator operator+(const LEncString::iterator& it, ptrdiff_t n)
{
    LEncString::iterator rv(it);
    rv += n;
    return it;
}

/**
 * Create an iterator placed at a particular position relative to another iterator.
 *
 * @param it  An iterator.
 * @param n   Steps to move, either negative or positive.
 *
 * @return An iterator referring to the new position. The result is undefined
 *         if @c n causes the iterator to conceptually move before the beginning
 *         of the string or beyond the end.
 */
inline LEncString::iterator operator+(ptrdiff_t n, const LEncString::iterator& it)
{
    return it + n;
}

/**
 * Create an iterator placed at a particular position relative to another iterator.
 *
 * @param it  An iterator.
 * @param n   Steps to move, either negative or positive.
 *
 * @return An iterator referring to the new position. The result is undefined
 *         if @c n causes the iterator to conceptually move before the beginning
 *         of the string or beyond the end.
 */
inline LEncString::iterator operator-(const LEncString::iterator& it, ptrdiff_t n)
{
    LEncString::iterator rv(it);
    rv -= n;
    return it;
}

}
