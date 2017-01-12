#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>
#include <ostream>
#include <sstream>
#include <string>
#include <maxscale/buffer.h>
#include <maxscale/mysql_utils.h>
#include <maxscale/protocol/mysql.h>

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
        m_value = mxs_leint_value(pData);
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
        size_t nBytes = mxs_leint_bytes(*ppData);
        m_value = mxs_leint_value(*ppData);
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
    operator uint64_t () const
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
inline std::ostream& operator << (std::ostream& out, const LEncInt& i)
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
    class iterator : public std::iterator<std::random_access_iterator_tag,
                                          char,
                                          std::ptrdiff_t,
                                          char*,
                                          char&>
    {
    public:
        iterator(char* pS)
            : m_pS(pS)
        {}

        iterator& operator++()
        {
            ss_dassert(m_pS);
            ++m_pS;
            return *this;
        }

        iterator operator++(int)
        {
            iterator rv(*this);
            ++(*this);
            return rv;
        }

        iterator& operator += (ptrdiff_t n)
        {
            ss_dassert(m_pS);
            m_pS += n;
            return *this;
        }

        iterator& operator -= (ptrdiff_t n)
        {
            ss_dassert(m_pS);
            m_pS -= n;
            return *this;
        }

        ptrdiff_t operator - (const iterator& rhs) const
        {
            ss_dassert(m_pS);
            ss_dassert(rhs.m_pS);
            return m_pS - rhs.m_pS;
        }

        bool operator == (const iterator& rhs) const
        {
            return m_pS == rhs.m_pS;
        }

        bool operator != (const iterator& rhs) const
        {
            return !(*this == rhs);
        }

        bool operator < (const iterator& rhs) const
        {
            return m_pS < rhs.m_pS;
        }

        bool operator <= (const iterator& rhs) const
        {
            return m_pS < rhs.m_pS;
        }

        bool operator > (const iterator& rhs) const
        {
            return m_pS > rhs.m_pS;
        }

        bool operator >= (const iterator& rhs) const
        {
            return m_pS > rhs.m_pS;
        }

        reference operator*()
        {
            ss_dassert(m_pS);
            return *m_pS;
        }

        reference operator[](ptrdiff_t i)
        {
            ss_dassert(m_pS);
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
    LEncString(uint8_t* pData)
    {
        // NULL is sent as 0xfb. See https://dev.mysql.com/doc/internals/en/com-query-response.html
        if (*pData != 0xfb)
        {
            m_pString = mxs_lestr_consume(&pData, &m_length);
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
    LEncString(uint8_t** ppData)
    {
        // NULL is sent as 0xfb. See https://dev.mysql.com/doc/internals/en/com-query-response.html
        if (**ppData != 0xfb)
        {
            m_pString = mxs_lestr_consume(ppData, &m_length);
        }
        else
        {
            m_pString = NULL;
            m_length = 0;
            ++(*ppData);
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
     * Compare for equality.
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
     * Compare for equality.
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
     * Compare for equality.
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
    char*  m_pString; /*<! Pointer to beginning of string, NOT zero-terminated. */
    size_t m_length;  /*<! Length of string. */
};

/**
 * Compare two strings for equality.
 *
 * @param lhs  A string.
 * @param rhs  Another string.
 *
 * @return True, if the strings are equal.
 */
inline bool operator == (const LEncString& lhs, const LEncString& rhs)
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
inline bool operator == (const std::string& lhs, const LEncString& rhs)
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
inline bool operator == (const LEncString& lhs, const std::string& rhs)
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
inline bool operator == (const LEncString& lhs, const char* zRhs)
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
inline std::ostream& operator << (std::ostream& out, const LEncString& s)
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
inline LEncString::iterator operator + (const LEncString::iterator& it, ptrdiff_t n)
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
inline LEncString::iterator operator + (ptrdiff_t n, const LEncString::iterator& it)
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
inline LEncString::iterator operator - (const LEncString::iterator& it, ptrdiff_t n)
{
    LEncString::iterator rv(it);
    rv -= n;
    return it;
}


class ComPacket
{
public:
    enum
    {
        OK_PACKET  = 0x00,
        EOF_PACKET = 0xfe,
        ERR_PACKET = 0xff,
    };

    uint32_t packet_len() const { return m_packet_len; }
    uint8_t packet_no() const { return m_packet_no; }

protected:
    ComPacket(GWBUF* pPacket)
        : m_pPacket(pPacket)
        , m_pI(GWBUF_DATA(pPacket))
        , m_packet_len(MYSQL_GET_PAYLOAD_LEN(m_pI))
        , m_packet_no(MYSQL_GET_PACKET_NO(m_pI))
    {
        m_pI += MYSQL_HEADER_LEN;
    }

    ComPacket(const ComPacket& packet)
        : m_pPacket(packet.m_pPacket)
        , m_pI(GWBUF_DATA(m_pPacket))
        , m_packet_len(packet.m_packet_len)
        , m_packet_no(packet.m_packet_no)
    {
        m_pI += MYSQL_HEADER_LEN;
    }

    GWBUF*   m_pPacket;
    uint8_t* m_pI;

private:
    uint32_t m_packet_len;
    uint8_t  m_packet_no;
};

class ComResponse : public ComPacket
{
public:
    ComResponse(GWBUF* pPacket)
        : ComPacket(pPacket)
        , m_type(*m_pI)
    {
        ++m_pI;
    }

    ComResponse(const ComResponse& packet)
        : ComPacket(packet)
        , m_type(packet.m_type)
    {
        ++m_pI;
    }

    uint8_t type() const
    {
        return m_type;
    }

    bool is_ok() const
    {
        return m_type == ComPacket::OK_PACKET;
    }

    bool is_eof() const
    {
        return m_type == ComPacket::EOF_PACKET;
    }

    bool is_err() const
    {
        return m_type == ComPacket::ERR_PACKET;
    }

protected:
    uint8_t m_type;
};

class ComRequest : public ComPacket
{
public:
    ComRequest(GWBUF* pPacket)
        : ComPacket(pPacket)
        , m_command(*m_pI)
    {
        ++m_pI;
    }

    uint8_t command() const { return m_command; }

protected:
    uint8_t m_command;
};

class ComQueryResponseColumnDef : public ComPacket
{
public:
    ComQueryResponseColumnDef(GWBUF* pPacket)
        : ComPacket(pPacket)
        , m_catalog(&m_pI)
        , m_schema(&m_pI)
        , m_table(&m_pI)
        , m_org_table(&m_pI)
        , m_name(&m_pI)
        , m_org_name(&m_pI)
        , m_length_fixed_fields(&m_pI)
    {
        m_character_set = *reinterpret_cast<const uint16_t*>(m_pI);
        m_pI += 2;

        m_column_length = *reinterpret_cast<const uint32_t*>(m_pI);
        m_pI += 4;

        m_type = *m_pI;
        m_pI += 1;

        m_flags = *reinterpret_cast<const uint16_t*>(m_pI);
        m_pI += 2;

        m_decimals = *m_pI;
        m_pI += 1;
    }

    const LEncString& catalog() const { return m_catalog; }
    const LEncString& schema() const { return m_schema; }
    const LEncString& table() const { return m_table; }
    const LEncString& org_table() const { return m_org_table; }
    const LEncString& name() const { return m_name; }
    const LEncString& org_name() const { return m_org_name; }

    std::string to_string() const
    {
        std::stringstream ss;
        ss << "\nCatalog      : " << m_catalog
           << "\nSchema       : " << m_schema
           << "\nTable        : " << m_table
           << "\nOrg table    : " << m_org_table
           << "\nName         : " << m_name
           << "\nOrd name     : " << m_org_name
           << "\nCharacer set : " << m_character_set
           << "\nColumn length: " << m_column_length
           << "\nType         : " << (uint16_t)m_type
           << "\nFlags        : " << m_flags
           << "\nDecimals     : " << (uint16_t)m_decimals;

        return ss.str();
    }

private:
    LEncString m_catalog;
    LEncString m_schema;
    LEncString m_table;
    LEncString m_org_table;
    LEncString m_name;
    LEncString m_org_name;
    LEncInt    m_length_fixed_fields;
    uint16_t   m_character_set;
    uint32_t   m_column_length;
    uint8_t    m_type;
    uint16_t   m_flags;
    uint8_t    m_decimals;
};

class ComQueryResponseRow : public ComPacket
{
public:
    class iterator : public std::iterator<std::forward_iterator_tag,
                                          LEncString,
                                          std::ptrdiff_t,
                                          LEncString*,
                                          LEncString>
    {
    public:
        iterator(uint8_t* pI = NULL)
            : m_pI(pI)
        {}

        iterator& operator++()
        {
            LEncString s(&m_pI);
            return *this;
        }

        iterator operator++(int)
        {
            iterator rv(*this);
            ++(*this);
            return rv;
        }

        bool operator == (const iterator& rhs) const
        {
            return m_pI == rhs.m_pI;
        }

        bool operator != (const iterator& rhs) const
        {
            return !(*this == rhs);
        }

        reference operator*()
        {
            return LEncString(m_pI);
        }

    private:
        uint8_t* m_pI;
    };

    ComQueryResponseRow(GWBUF* pPacket)
        : ComPacket(pPacket)
    {
    }

    ComQueryResponseRow(const ComResponse& packet)
        : ComPacket(packet)
    {
    }

    iterator begin()
    {
        return iterator(m_pI);
    }

    iterator end()
    {
        uint8_t* pEnd = GWBUF_DATA(m_pPacket) + GWBUF_LENGTH(m_pPacket);
        return iterator(pEnd);
    }
};

class ComQueryResponse : public ComPacket
{
public:
    typedef ComQueryResponseColumnDef ColumnDef;
    typedef ComQueryResponseRow       Row;

    ComQueryResponse(GWBUF* pPacket)
        : ComPacket(pPacket)
        , m_nFields(&m_pI)
    {
    }

    ComQueryResponse(const ComResponse& packet)
        : ComPacket(packet)
        , m_nFields(&m_pI)
    {
    }

    uint64_t nFields() const { return m_nFields; }

private:
    LEncInt m_nFields;
};
