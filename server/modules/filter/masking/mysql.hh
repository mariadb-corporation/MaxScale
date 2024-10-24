/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>
#include <mysql.h>
#include <maxsql/mariadb.hh>
#include <maxscale/buffer.hh>
#include <maxscale/protocol/mariadb/mysql.hh>

/**
 * @class ComPacket
 *
 * Base-class of all packet classes.
 */
class ComPacket
{
public:
    enum
    {
        MAX_PAYLOAD_LEN = 0xffffff
    };

    explicit ComPacket(uint8_t* pBuffer)
        : ComPacket(pBuffer, packet_len(pBuffer))
    {
    }

    explicit ComPacket(uint8_t** ppBuffer)
        : ComPacket(*ppBuffer)
    {
        *ppBuffer += packet_len();
    }

    explicit ComPacket(uint8_t** ppBuffer, size_t nBuffer)
        : ComPacket(*ppBuffer, nBuffer)
    {
        *ppBuffer += packet_len();
    }

    ComPacket(uint8_t* pBuffer, uint32_t nBuffer)
        : m_pBuffer(pBuffer)
        , m_nBuffer(nBuffer)
        , m_pData(pBuffer + MYSQL_HEADER_LEN)
        , m_payload_len(MYSQL_GET_PAYLOAD_LEN(m_pBuffer))
        , m_packet_no(MYSQL_GET_PACKET_NO(m_pBuffer))
    {
        mxb_assert(nBuffer >= MYSQL_HEADER_LEN + m_payload_len);
    }

    ComPacket(GWBUF& buffer)
        : ComPacket(buffer.data(), buffer.length())
    {
    }

    explicit ComPacket(GWBUF* pPacket)
        : ComPacket(pPacket->data(), pPacket->length())
    {
    }

    ComPacket(const ComPacket& that)
        : m_pBuffer(that.m_pBuffer)
        , m_nBuffer(that.m_nBuffer)
        , m_pData(m_pBuffer + MYSQL_HEADER_LEN)
        , m_payload_len(that.m_payload_len)
        , m_packet_no(that.m_packet_no)
    {
    }

    uint8_t* buffer() const
    {
        return m_pBuffer;
    }

    uint32_t payload_len() const
    {
        return m_payload_len;
    }

    uint32_t packet_len() const
    {
        return MYSQL_HEADER_LEN + m_payload_len;
    }

    static uint32_t packet_len(uint8_t* pBuffer)
    {
        return MYSQL_HEADER_LEN + MYSQL_GET_PAYLOAD_LEN(pBuffer);
    }

    uint8_t packet_no() const
    {
        return m_packet_no;
    }

protected:
    uint8_t* m_pBuffer;
    uint32_t m_nBuffer;
    uint8_t* m_pData;

    uint32_t m_payload_len;
    uint8_t  m_packet_no;
};


/**
 * @class ComResponse
 *
 * Base-class of all response packet classes.
 */
class ComResponse : public ComPacket
{
public:
    enum
    {
        OK_PACKET           = MYSQL_REPLY_OK,           // 0x00
        EOF_PACKET          = MYSQL_REPLY_EOF,          // 0xfe
        ERR_PACKET          = MYSQL_REPLY_ERR,          // 0xff
        LOCAL_INFILE_PACKET = MYSQL_REPLY_LOCAL_INFILE, // 0xfb
        UNKNOWN_PACKET      = 42
    };

    explicit ComResponse(uint8_t* pBuffer)
        : ComPacket(pBuffer)
        , m_type(get_type())
    {
        mxb_assert(packet_len() >= MYSQL_HEADER_LEN + 1);
        ++m_pData;
    }

    explicit ComResponse(uint8_t* pBuffer, size_t nBuffer)
        : ComPacket(pBuffer, nBuffer)
        , m_type(get_type())
    {
        mxb_assert(packet_len() >= MYSQL_HEADER_LEN + 1);
        ++m_pData;
    }

    explicit ComResponse(uint8_t** ppBuffer)
        : ComPacket(ppBuffer)
        , m_type(get_type())
    {
        mxb_assert(packet_len() >= MYSQL_HEADER_LEN + 1);
        ++m_pData;
    }

    explicit ComResponse(GWBUF* pPacket)
        : ComPacket(pPacket)
        , m_type(get_type())
    {
        mxb_assert(packet_len() >= MYSQL_HEADER_LEN + 1);
        ++m_pData;
    }

    ComResponse(GWBUF& buffer)
        : ComPacket(buffer)
        , m_type(get_type())
    {
        mxb_assert(packet_len() >= MYSQL_HEADER_LEN + 1);
        ++m_pData;
    }

    ComResponse(const ComPacket& packet)
        : ComPacket(packet)
        , m_type(get_type())
    {
        mxb_assert(packet_len() >= MYSQL_HEADER_LEN + 1);
        ++m_pData;
    }

    ComResponse(const ComResponse& packet)
        : ComPacket(packet)
        , m_type(packet.m_type)
    {
        mxb_assert(packet_len() >= MYSQL_HEADER_LEN + 1);
        ++m_pData;
    }

    uint8_t type() const
    {
        return m_type;
    }

    bool is_ok() const
    {
        return m_type == ComResponse::OK_PACKET;
    }

    bool is_eof() const
    {
        return m_type == ComResponse::EOF_PACKET;
    }

    bool is_err() const
    {
        return m_type == ComResponse::ERR_PACKET;
    }

protected:
    uint8_t get_type()
    {
        uint8_t type = *m_pData;

        switch (type)
        {
        case OK_PACKET:
        case ERR_PACKET:
        case LOCAL_INFILE_PACKET:
        case EOF_PACKET:
            if (m_payload_len == MAX_PAYLOAD_LEN)
            {
                type = UNKNOWN_PACKET;
            }
            break;

        default:
            type = UNKNOWN_PACKET;
        }

        return type;
    }

    uint8_t m_type;
};

class ComEOF : public ComResponse
{
public:
    enum
    {
        PACKET_LEN  = MYSQL_EOF_PACKET_LEN,
        PAYLOAD_LEN = MYSQL_EOF_PACKET_LEN - MYSQL_HEADER_LEN
    };

    ComEOF(GWBUF* pPacket)
        : ComResponse(pPacket)
    {
        mxb_assert(m_type == EOF_PACKET);

        extract_payload();
    }

    ComEOF(const ComResponse& response)
        : ComResponse(response)
    {
        mxb_assert(m_type == EOF_PACKET);

        extract_payload();
    }

    uint16_t warnings() const
    {
        return m_warnings;
    }

    uint16_t status() const
    {
        return m_status;
    }

private:
    void extract_payload()
    {
        m_warnings = mariadb::get_byte2(m_pData);
        m_pData += 2;

        m_status = mariadb::get_byte2(m_pData);
        m_pData += 2;
    }

private:
    uint16_t m_warnings;
    uint16_t m_status;
};

class ComERR : public ComResponse
{
public:
    ComERR(GWBUF* pPacket)
        : ComResponse(pPacket)
    {
        mxb_assert(m_type == ERR_PACKET);

        extract_payload();
    }

    ComERR(const ComResponse& response)
        : ComResponse(response)
    {
        mxb_assert(m_type == ERR_PACKET);

        extract_payload();
    }

    uint16_t code() const
    {
        return m_error_code;
    }

    std::string state() const
    {
        return std::string(m_pData, m_pData + 5);
    }

    std::string message() const
    {
        return std::string(m_pData + 5, m_pBuffer + MYSQL_HEADER_LEN + m_payload_len);
    }

private:
    void extract_payload()
    {
        m_error_code = *m_pData++;
        m_error_code += (*m_pData++ << 8);

        ++m_pData; // Bypass the state marker.
    }

    uint16_t m_error_code;
};

class ComOK : public ComResponse
{
public:
    ComOK(GWBUF* pPacket)
        : ComResponse(pPacket)
        , m_affected_rows(&m_pData)
        , m_last_insert_id(&m_pData)
        , m_status(mariadb::consume_byte2(&m_pData))
        , m_warnings(mariadb::consume_byte2(&m_pData))
        , m_info(&m_pData, m_pBuffer + MYSQL_HEADER_LEN + m_payload_len - m_pData)
    {
        mxb_assert(m_type == OK_PACKET);
        mxb_assert(m_pData <= m_pBuffer + MYSQL_HEADER_LEN + m_payload_len);
    }

    ComOK(const ComResponse& response)
        : ComResponse(response)
        , m_affected_rows(&m_pData)
        , m_last_insert_id(&m_pData)
        , m_status(mariadb::consume_byte2(&m_pData))
        , m_warnings(mariadb::consume_byte2(&m_pData))
        , m_info(&m_pData, m_pBuffer + MYSQL_HEADER_LEN + m_payload_len - m_pData)
    {
        mxb_assert(m_type == OK_PACKET);
        mxb_assert(m_pData <= m_pBuffer + MYSQL_HEADER_LEN + m_payload_len);
    }

    uint64_t affected_rows() const
    {
        return m_affected_rows;
    }

    uint64_t last_insert_id() const
    {
        return m_last_insert_id;
    }

    uint16_t warnings() const
    {
        return m_warnings;
    }

    uint16_t status() const
    {
        return m_status;
    }

    const mxq::LEncString& info() const
    {
        return m_info;
    }

    uint64_t matched_rows() const
    {
        uint64_t rv = 0;

        std::string s = m_info.to_string();

        // An OK from a DELETE will e.g. be empty.
        if (!s.empty())
        {
            auto i = s.find("Rows matched: ");
            mxb_assert(i == 0);

            if (i != std::string::npos)
            {
                mxb_assert(s.find("  Changed:") != 0);

                char* zEnd;
                rv = strtoul(s.c_str() + 14, &zEnd, 10); // strlen("Rows matched: ") == 14

                mxb_assert(zEnd - s.c_str() == (long)s.find("  Changed:"));
            }
        }

        return rv;
    }

private:
    mxq::LEncInt    m_affected_rows;
    mxq::LEncInt    m_last_insert_id;
    uint16_t        m_status;
    uint16_t        m_warnings;
    mxq::LEncString m_info;
};

/**
 * @class ComRequest
 *
 * Base-class of all request packet classes.
 */
class ComRequest : public ComPacket
{
public:
    ComRequest(GWBUF* pPacket)
        : ComPacket(pPacket)
        , m_command(*m_pData)
    {
        ++m_pData;
    }

    uint8_t command() const
    {
        return m_command;
    }

protected:
    uint8_t m_command;
};

/**
 * @class CQRColumnDef
 *
 * The column definition of the response of a @c ComQuery.
 *
 * @attention The name should not be used as such, but always using the
 *            typedef @c ComQueryResponse::ColumnDef.
 */
class CQRColumnDef : public ComPacket
{
public:
    enum class Protocol {DEFAULT, EXTENDED_TYPES};

    CQRColumnDef(uint8_t** ppBuffer, Protocol p = Protocol::DEFAULT)
        : ComPacket(ppBuffer)
        , m_catalog(&m_pData)
        , m_schema(&m_pData)
        , m_table(&m_pData)
        , m_org_table(&m_pData)
        , m_name(&m_pData)
        , m_org_name(&m_pData)
        , m_extended_type_info(&m_pData, p == Protocol::DEFAULT ? 0 : std::numeric_limits<size_t>::max())
        , m_length_fixed_fields(&m_pData)
    {
        m_character_set = mariadb::get_byte2(m_pData);
        m_pData += 2;

        m_column_length = mariadb::get_byte4(m_pData);
        m_pData += 4;

        m_type = static_cast<enum_field_types>(*m_pData);
        m_pData += 1;

        m_flags = mariadb::get_byte2(m_pData);
        m_pData += 2;

        m_decimals = *m_pData;
        m_pData += 1;
    }

    CQRColumnDef(uint8_t* pBuffer, Protocol p = Protocol::DEFAULT)
        : CQRColumnDef(&pBuffer, p)
    {
    }

    CQRColumnDef(GWBUF* pPacket, Protocol p = Protocol::DEFAULT)
        : CQRColumnDef(pPacket->data(), p)
    {
    }

    const mxq::LEncString& catalog() const
    {
        return m_catalog;
    }
    const mxq::LEncString& schema() const
    {
        return m_schema;
    }
    const mxq::LEncString& table() const
    {
        return m_table;
    }
    const mxq::LEncString& org_table() const
    {
        return m_org_table;
    }
    const mxq::LEncString& name() const
    {
        return m_name;
    }
    const mxq::LEncString& org_name() const
    {
        return m_org_name;
    }
    const mxq::LEncString& extended_type_info() const
    {
        return m_extended_type_info;
    }
    enum_field_types type() const
    {
        return m_type;
    }

    std::string to_string() const
    {
        std::stringstream ss;
        ss << "\nCatalog      : " << m_catalog
           << "\nSchema       : " << m_schema
           << "\nTable        : " << m_table
           << "\nOrg table    : " << m_org_table
           << "\nName         : " << m_name
           << "\nOrd name     : " << m_org_name
           << "\nCharacter set: " << m_character_set
           << "\nColumn length: " << m_column_length
           << "\nType         : " << (uint16_t)m_type
           << "\nFlags        : " << m_flags
           << "\nDecimals     : " << (uint16_t)m_decimals;

        return ss.str();
    }

private:
    mxq::LEncString  m_catalog;
    mxq::LEncString  m_schema;
    mxq::LEncString  m_table;
    mxq::LEncString  m_org_table;
    mxq::LEncString  m_name;
    mxq::LEncString  m_org_name;
    mxq::LEncString  m_extended_type_info;
    mxq::LEncInt     m_length_fixed_fields;
    uint16_t         m_character_set;
    uint32_t         m_column_length;
    enum_field_types m_type;
    uint16_t         m_flags;
    uint8_t          m_decimals;
};

/**
 * @class CQRResultsetValue
 *
 * An instance of this class represents a value in a resultset row. As this
 * currently is for the purpose of the masking filter, it effectively is useful
 * for accessing NULL and string values.
 *
 * @attention The name should not be used as such, but instead either
 *            @c ComQueryResponse::TextResultsetRow::Value or
 *            @c ComQueryResponse::TextResultsetRow::Value.
 */
class CQRResultsetValue
{
public:
    CQRResultsetValue()
        : m_type(MYSQL_TYPE_NULL)
        , m_pData(NULL)
    {
    }

    CQRResultsetValue(enum_field_types type, uint8_t* pData)
        : m_type(type)
        , m_pData(pData)
    {
    }

    enum_field_types type() const
    {
        return m_type;
    }

    mxq::LEncString as_string() const
    {
        //Generally possible if textual protocol, only for string types if binary.
        return mxq::LEncString(m_pData);
    }

    bool is_null() const
    {
        return m_type == MYSQL_TYPE_NULL;
    }

protected:
    enum_field_types m_type;
    uint8_t*         m_pData;
};

/**
 * @class CQRTextResultsetValue
 *
 * An instance of this class represents a value in a textual resultset row.
 *
 * @attention The name should not be used as such, but always using the
 *            typedef @c ComQueryResponse::TextResultsetRow::Value.
 */
class CQRTextResultsetValue : public CQRResultsetValue
{
public:
    CQRTextResultsetValue(enum_field_types type, uint8_t* pData)
        : CQRResultsetValue(type, pData)
    {
        if (*pData == 0xfb)
        {
            m_type = MYSQL_TYPE_NULL;
        }
    }
};

/**
 * @class CQRBinaryResultsetValue
 *
 * An instance of this class represents a value in a binary resultset row.
 *
 * @attention The name should not be used as such, but always using the
 *            typedef @c ComQueryResponse::BinaryResultsetRow::Value.
 */
typedef CQRResultsetValue CQRBinaryResultsetValue;

/**
 * @class CQRTextResultsetRowIterator
 *
 * An STL compatible iterator that iterates over the values in a textual resultset.
 *
 * @attention The name should not be used as such, but always using the
 *            typedef @c ComQueryResponse::TextResultset::iterator.
 */
class CQRTextResultsetRowIterator
{
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = CQRTextResultsetValue;
    using difference_type = std::ptrdiff_t;
    using pointer = CQRTextResultsetValue*;
    using reference = CQRTextResultsetValue;

    typedef CQRTextResultsetValue Value;

    CQRTextResultsetRowIterator(uint8_t* pData, const std::vector<enum_field_types>& types)
        : m_pData(pData)
        , m_iTypes(types.begin())
    {
    }

    CQRTextResultsetRowIterator(uint8_t* pData)
        : m_pData(pData)
    {
    }

    CQRTextResultsetRowIterator& operator++()
    {
        // In the textual protocol, every value is a length encoded string.
        mxq::LEncString s(&m_pData);
        ++m_iTypes;
        return *this;
    }

    CQRTextResultsetRowIterator operator++(int)
    {
        CQRTextResultsetRowIterator rv(*this);
        ++(*this);
        return rv;
    }

    bool operator==(const CQRTextResultsetRowIterator& rhs) const
    {
        return m_pData == rhs.m_pData;
    }

    bool operator!=(const CQRTextResultsetRowIterator& rhs) const
    {
        return !(*this == rhs);
    }

    CQRTextResultsetValue operator*()
    {
        return Value(*m_iTypes, m_pData);
    }

private:
    uint8_t*                                      m_pData;
    std::vector<enum_field_types>::const_iterator m_iTypes;
};

/**
 * @class CQRBinaryResultsetRowIterator
 *
 * An STL compatible iterator that iterates over the values in a binary resultset.
 *
 * @attention The name should not be used as such, but always using the
 *            typedef @c ComQueryResponse::BinaryResultset::iterator.
 */
class CQRBinaryResultsetRowIterator
{
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = CQRBinaryResultsetValue;
    using difference_type = std::ptrdiff_t;
    using pointer = CQRBinaryResultsetValue*;
    using reference = CQRBinaryResultsetValue;

    typedef CQRBinaryResultsetValue Value;

    /**
     * A bit_iterator is an iterator to bits in an array of bytes.
     *
     * Specifically, it is capable of iterating across the NULL bitmask of
     * a binary resultset.
     */
    class bit_iterator
    {
    public:
        bit_iterator(uint8_t* pData = 0)
            : m_pData(pData)
            , m_mask(1 << 2)// The two first bits are not used.
        {
        }

        /**
         * @return True, if the current bit is on. That is, if the corresponding
         *         column value is NULL.
         */
        bool operator*() const
        {
            return (*m_pData & m_mask) ? true : false;
        }

        bit_iterator& operator++()
        {
            m_mask <<= 1;   // Move to the next bit.
            if (m_mask == 0)
            {
                // We moved past the byte, so advance to next byte and the first bit of that.
                ++m_pData;
                m_mask = 1;
            }

            return *this;
        }

        bit_iterator operator++(int)
        {
            bit_iterator rv(*this);
            ++(*this);
            return rv;
        }

    private:
        uint8_t* m_pData;   /*< Pointer to the NULL bitmap of a binary resultset row. */
        uint8_t  m_mask;    /*< Mask representing the current bit of the current byte. */
    };

    CQRBinaryResultsetRowIterator(uint8_t* pData, const std::vector<enum_field_types>& types)
        : m_pData(pData)
        , m_iTypes(types.begin())
        , m_iNulls(pData + 1)
    {
        mxb_assert(*m_pData == 0);
        ++m_pData;

        // See https://dev.mysql.com/doc/internals/en/binary-protocol-resultset-row.html
        size_t nNull_bytes = (types.size() + 7 + 2) / 8;

        m_pData += nNull_bytes;
    }

    CQRBinaryResultsetRowIterator(uint8_t* pData)
        : m_pData(pData)
    {
    }

    CQRBinaryResultsetRowIterator& operator++()
    {
        // See https://dev.mysql.com/doc/internals/en/binary-protocol-value.html
        switch (*m_iTypes)
        {
        case MYSQL_TYPE_BIT:
        case MYSQL_TYPE_BLOB:
        case MYSQL_TYPE_DECIMAL:
        case MYSQL_TYPE_ENUM:
        case MYSQL_TYPE_GEOMETRY:
        case MYSQL_TYPE_LONG_BLOB:
        case MYSQL_TYPE_MEDIUM_BLOB:
        case MYSQL_TYPE_NEWDATE:
        case MYSQL_TYPE_NEWDECIMAL:
        case MYSQL_TYPE_SET:
        case MYSQL_TYPE_STRING:
        case MYSQL_TYPE_TINY_BLOB:
        case MYSQL_TYPE_VARCHAR:
        case MYSQL_TYPE_VAR_STRING:
            {
                mxq::LEncString s(&m_pData);     // Advance m_pData to the byte following the string.
            }
            break;

        case MYSQL_TYPE_LONGLONG:
            m_pData += 8;
            break;

        case MYSQL_TYPE_LONG:
        case MYSQL_TYPE_INT24:
            m_pData += 4;
            break;

        case MYSQL_TYPE_SHORT:
        case MYSQL_TYPE_YEAR:
            m_pData += 2;
            break;

        case MYSQL_TYPE_TINY:
            m_pData += 1;
            break;

        case MYSQL_TYPE_DOUBLE:
            m_pData += 8;
            break;

        case MYSQL_TYPE_FLOAT:
            m_pData += 4;
            break;

        case MYSQL_TYPE_DATE:
        case MYSQL_TYPE_DATETIME:
        case MYSQL_TYPE_TIMESTAMP:
            {
                // A byte specifying the length, followed by that many bytes.
                // Either 0, 4, 7 or 11.
                uint8_t len = *m_pData++;
                m_pData += len;
            }
            break;

        case MYSQL_TYPE_TIME:
            {
                // A byte specifying the length, followed by that many bytes.
                // Either 0, 8 or 12.
                uint8_t len = *m_pData++;
                m_pData += len;
            }
            break;

        case MYSQL_TYPE_NULL:
            break;

        case MAX_NO_FIELD_TYPES:
            mxb_assert(!true);
            break;

        default:
            break;
        }

        ++m_iNulls;
        ++m_iTypes;

        return *this;
    }

    CQRBinaryResultsetRowIterator operator++(int)
    {
        CQRBinaryResultsetRowIterator rv(*this);
        ++(*this);
        return rv;
    }

    bool operator==(const CQRBinaryResultsetRowIterator& rhs) const
    {
        return m_pData == rhs.m_pData;
    }

    bool operator!=(const CQRBinaryResultsetRowIterator& rhs) const
    {
        return !(*this == rhs);
    }

    reference operator*()
    {
        if (*m_iNulls)
        {
            return Value();
        }
        else
        {
            return Value(*m_iTypes, m_pData);
        }
    }

private:
    uint8_t*                                      m_pData;
    std::vector<enum_field_types>::const_iterator m_iTypes;
    bit_iterator                                  m_iNulls;
};

/**
 * @template CQRResultsetRow
 *
 * A template that when instantiated either represents a textual or a
 * binary resultset row.
 */
template<class Iterator>
class CQRResultsetRow : public ComPacket
{
public:
    typedef typename Iterator::Value Value;
    typedef Iterator                 iterator;

    CQRResultsetRow(uint8_t* pBuffer,
                    const std::vector<enum_field_types>& types)
        : ComPacket(pBuffer)
        , m_types(types)
    {
    }

    CQRResultsetRow(uint8_t** ppBuffer,
                    const std::vector<enum_field_types>& types)
        : ComPacket(ppBuffer)
        , m_types(types)
    {
    }

    /**
     * Constructor for the case that the buffer contains a resultset
     * that may consist of multiple packets. If so, the packets belonging
     * to the resultset will be "flattened", that is, the header of each
     * subsequent packet is removed and the data moved, so that the resultset
     * data is in one contiguous chunk.
     *
     * @param ppBuffer  Pointer to pointer to the first packet. On return,
     *                  the pointer will point to the first packet following
     *                  the resultset.
     * @param pnBuffer  Pointer to size of the buffer. On return, the value
     *                  will be what it unconsumed from the buffer.
     */
    CQRResultsetRow(uint8_t** ppBuffer,
                    size_t* pnBuffer,
                    const std::vector<enum_field_types>& types)
        : ComPacket(ppBuffer, *pnBuffer)
        , m_types(types)
    {
        auto packet_len = flatten();

        *ppBuffer = m_pBuffer + packet_len;
        *pnBuffer -= packet_len;
    }

    CQRResultsetRow(GWBUF* pPacket,
                    const std::vector<enum_field_types>& types)
        : ComPacket(pPacket)
        , m_types(types)
    {
    }

    CQRResultsetRow(const ComResponse& packet,
                    const std::vector<enum_field_types>& types)
        : ComPacket(packet)
        , m_types(types)
    {
    }

    iterator begin() const
    {
        return iterator(m_pData, m_types);
    }

    iterator end() const
    {
        uint8_t* pEnd = m_pBuffer + MYSQL_HEADER_LEN + m_payload_len;
        return iterator(pEnd);
    }

private:
    uint32_t flatten()
    {
        uint32_t packet_len = 0;

        if (m_payload_len == MAX_PAYLOAD_LEN)
        {
            int32_t nPayload = m_payload_len;

            uint8_t* pData = m_pBuffer + MYSQL_HEADER_LEN + nPayload;
            uint8_t* pPacket = pData;
            uint8_t* end = m_pBuffer + m_nBuffer;

            do
            {
                mxb_assert(pPacket < end);

                nPayload = MYSQL_GET_PAYLOAD_LEN(pPacket);

                memmove(pData, pPacket + MYSQL_HEADER_LEN, nPayload);

                pData += nPayload;
                pPacket += MYSQL_HEADER_LEN + nPayload;

                m_payload_len += nPayload;
            }
            while (nPayload == MAX_PAYLOAD_LEN);

            packet_len = (pPacket - m_pBuffer);
        }
        else
        {
            packet_len = MYSQL_HEADER_LEN + m_payload_len;
        }

        return packet_len;
    }

    const std::vector<enum_field_types>& m_types;
};

/**
 * @class CQRTextResultsetRow
 *
 * An instance of this class represents a textual resultset row.
 */
typedef CQRResultsetRow<CQRTextResultsetRowIterator> CQRTextResultsetRow;

/**
 * @class CQRBinaryResultsetRow
 *
 * An instance of this class represents a binary resultset row.
 */
typedef CQRResultsetRow<CQRBinaryResultsetRowIterator> CQRBinaryResultsetRow;

/**
 * @class ComQueryResponse
 *
 * An instance of this class represents the response to a @c ComQuery.
 */
class ComQueryResponse : public ComPacket
{
public:
    typedef CQRColumnDef          ColumnDef;
    typedef CQRTextResultsetRow   TextResultsetRow;
    typedef CQRBinaryResultsetRow BinaryResultsetRow;

    ComQueryResponse(uint8_t** ppBuffer)
        : ComPacket(ppBuffer)
        , m_nFields(&m_pData)
    {
    }

    ComQueryResponse(uint8_t* pBuffer)
        : ComQueryResponse(&pBuffer)
    {
    }

    ComQueryResponse(GWBUF* pPacket)
        : ComQueryResponse(pPacket->data())
    {
    }

    ComQueryResponse(const ComResponse& packet)
        : ComQueryResponse(packet.buffer())
    {
    }

    uint64_t nFields() const
    {
        return m_nFields;
    }

private:
    mxq::LEncInt m_nFields;
};
