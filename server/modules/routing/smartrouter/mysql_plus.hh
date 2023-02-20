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
#include <ostream>
#include <sstream>
#include <string>
#include <vector>
#include <mysql.h>
#include <maxsql/mariadb.hh>
#include <maxscale/buffer.hh>
#include <maxscale/protocol/mariadb/mysql.hh>

namespace maxsql
{

/**
 * @class ComPacket
 *
 * Base-class of all packet classes.
 *
 * TODO: Document this. ComPacket (and it's derived class ComResponse) implement key protocol support.
 */
class ComPacket
{
public:
    // For the lifetime of a packet stream (query, response), the caller must pass in a bool* for each
    // successive call, initialized to false before the first call. This is used to track split packets,
    // but the client should use the is_split_xx() functions and not assume anything about the bool.
    ComPacket(GWBUF* pPacket, bool* client_split_flag)
        : m_pPayload(GWBUF_DATA(pPacket))
        , m_payload_len(MYSQL_GET_PAYLOAD_LEN(m_pPayload))
        , m_packet_no(MYSQL_GET_PACKET_NO(m_pPayload))
        , m_split_flag_at_entry(*client_split_flag)
    {
        m_pPayload += MYSQL_HEADER_LEN;

        bool at_max = (m_payload_len == GW_MYSQL_MAX_PACKET_LEN);
        if (!m_split_flag_at_entry && at_max)
        {
            *client_split_flag = true;      // first split packet
        }
        else if (m_split_flag_at_entry && !at_max)
        {
            *client_split_flag = false;     // last split packet
        }
    }

    uint8_t* payload() const
    {
        return m_pPayload;
    }

    uint32_t payload_len() const
    {
        return m_payload_len;
    }

    uint32_t packet_len() const
    {
        return MYSQL_HEADER_LEN + m_payload_len;
    }

    uint8_t packet_no() const
    {
        return m_packet_no;
    }

    // true if this packet is the first one of a split
    bool is_split_leader() const
    {
        return !m_split_flag_at_entry && m_payload_len == GW_MYSQL_MAX_PACKET_LEN;
    }

    // true if this packet is part of a split, but not the leader. This is the only
    // split function a client needs to use, to know to pass continuation data through.
    bool is_split_continuation() const
    {
        return m_split_flag_at_entry;
    }

    // true if this is the last packet of a split
    bool is_split_trailer() const
    {
        return m_split_flag_at_entry && m_payload_len < GW_MYSQL_MAX_PACKET_LEN;
    }
private:
    uint8_t* m_pPayload;
    uint32_t m_payload_len;
    uint8_t  m_packet_no;
    bool     m_split_flag_at_entry;
};

/**
 * @class ComResponse
 *
 * Base-class of all response packet classes.
 *
 * TODO: Document this class.
 *       The is_some_type() functions are mutually exclusive.
 *
 */
class ComResponse : public ComPacket
{
public:
    enum class Type {Ok, Err, Eof, LocalInfile, Data};

    // The client has to specify when it is expecting a packet without a cmd byte. See the meaning
    // of different Types in member functions below.
    ComResponse(const ComPacket& packet, bool expecting_data_only)
        : ComPacket(packet)
    {
        if (*payload() == MYSQL_REPLY_ERR)
        {
            m_type = Type::Err;
            m_payload_offset = 1;
        }
        else if (is_split_continuation())
        {
            m_type = Type::Data;
            m_payload_offset = 0;
        }
        else if (packet_len() == MYSQL_EOF_PACKET_LEN && *payload() == MYSQL_REPLY_EOF)
        {
            m_type = Type::Eof;
            m_payload_offset = 1;
        }
        else if (expecting_data_only)
        {
            m_type = Type::Data;
            m_payload_offset = 0;
        }
        else
        {   // A first payload byte of 0xfb always means local infile in this context, assuming the client
            // sets expecting_data_only=true appropriately.

            m_payload_offset = 1;   // tentatively

            switch (*payload())
            {
            case MYSQL_REPLY_OK:
                m_type = Type::Ok;
                break;

            case MYSQL_REPLY_LOCAL_INFILE:
                m_type = Type::LocalInfile;
                break;

            default:
                m_type = Type::Data;
                m_payload_offset = 0;
                break;
            }
        }
    }

    // Ptr to the data of this packet. This is only meant for reading simple upfront data. See class Buffer.
    uint8_t* data(int index = 0)
    {
        return payload() + m_payload_offset + index;
    }

    Type type() const
    {
        return m_type;
    }

    // Ok is not set when expecting_data_only==true (an Ok would be Data).
    bool is_ok() const
    {
        return m_type == Type::Ok;
    }

    // any packet can be an eof
    bool is_eof() const
    {
        return m_type == Type::Eof;
    }

    // any packet can be an error
    bool is_err() const
    {
        return m_type == Type::Err;
    }

    // LocalInfile is not set when expecting_data_only==true (a LocalInfile would be Data).
    bool is_local_infile() const
    {
        return m_type == Type::LocalInfile;
    }

    // The type is Data if:
    // 1. expecting_data_only==true and this packet is not an ERR or EOF.
    // 2. expecting_data_only!=true and the packet is none of generic packets (Ok, Err, Eof) or LocalInfile.
    // 3. This packet is a split continuation. However, for split handling the client should use the split
    //    functions provided by ComPacket, usually ComPacket::is_split_continuation().
    bool is_data() const
    {
        return m_type == Type::Data;
    }
private:
    Type    m_type;
    uint8_t m_payload_offset;
};

inline std::ostream& operator<<(std::ostream& os, ComResponse::Type type)
{
    static const std::array<std::string, 6> type_names = {
        "Ok", "Err", "Eof", "LocalInfile", "Data"
    };

    auto ind = static_cast<size_t>(type);
    return os << ((ind < type_names.size()) ? type_names[ind] : "UNKNOWN");
}

class ComEOF : public ComResponse
{
public:
    explicit ComEOF(const ComResponse& response)
        : ComResponse(response)
    {
        mxb_assert(is_eof());

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

    bool more_results_exist()
    {
        return m_status & SERVER_MORE_RESULTS_EXIST;
    }

private:
    void extract_payload()
    {
        auto pData = data();
        m_warnings = *pData++;
        m_warnings += (*pData++ << 8);

        m_status = *pData++;
        m_status += (*pData++ << 8);
    }

private:
    uint16_t m_warnings;
    uint16_t m_status;
};

class ComOK : public ComResponse
{
public:
    explicit ComOK(const ComResponse& response)
        : ComResponse(response)
    {
        mxb_assert(is_ok());

        extract_payload();
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

    bool more_results_exist()
    {
        return m_status & SERVER_MORE_RESULTS_EXIST;
    }
private:
    void extract_payload()
    {
        auto pData = data();

        m_affected_rows = LEncInt(&pData).value();
        m_last_insert_id = LEncInt(&pData).value();

        m_status = *pData++;
        m_status += (*pData++ << 8);

        m_warnings = *pData++;
        m_warnings += (*pData++ << 8);
    }

private:
    uint64_t m_affected_rows;
    uint64_t m_last_insert_id;
    uint16_t m_status;
    uint16_t m_warnings;
};

/**
 * @class ComRequest
 *
 * Base-class of all request packet classes.
 */
class ComRequest : public ComPacket
{
public:
    explicit ComRequest(const ComPacket& com_packet)
        : ComPacket(com_packet)
        , m_command(*payload())
    {
    }

    uint8_t* data()
    {
        return payload() + 1;
    }

    uint8_t command() const
    {
        return m_command;
    }

    bool server_will_respond() const
    {
        return m_command != MXS_COM_STMT_SEND_LONG_DATA     // what?
               && m_command != MXS_COM_QUIT
               && m_command != MXS_COM_STMT_CLOSE;
    }
private:
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
    CQRColumnDef(const ComPacket& com_packet)
        : ComPacket(com_packet)
        , m_pData(payload())
        , m_catalog(&m_pData)
        , m_schema(&m_pData)
        , m_table(&m_pData)
        , m_org_table(&m_pData)
        , m_name(&m_pData)
        , m_org_name(&m_pData)
        , m_length_fixed_fields(&m_pData)
    {
        m_character_set = *reinterpret_cast<const uint16_t*>(m_pData);
        m_pData += 2;

        m_column_length = *reinterpret_cast<const uint32_t*>(m_pData);
        m_pData += 4;

        m_type = static_cast<enum_field_types>(*m_pData);
        m_pData += 1;

        m_flags = *reinterpret_cast<const uint16_t*>(m_pData);
        m_pData += 2;

        m_decimals = *m_pData;
        m_pData += 1;
    }

    const LEncString& catalog() const
    {
        return m_catalog;
    }
    const LEncString& schema() const
    {
        return m_schema;
    }
    const LEncString& table() const
    {
        return m_table;
    }
    const LEncString& org_table() const
    {
        return m_org_table;
    }
    const LEncString& name() const
    {
        return m_name;
    }
    const LEncString& org_name() const
    {
        return m_org_name;
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
    uint8_t*         m_pData;
    LEncString       m_catalog;
    LEncString       m_schema;
    LEncString       m_table;
    LEncString       m_org_table;
    LEncString       m_name;
    LEncString       m_org_name;
    LEncInt          m_length_fixed_fields;
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

    LEncString as_string()
    {
        mxb_assert(is_string());
        return LEncString(m_pData);
    }

    bool is_null() const
    {
        return m_type == MYSQL_TYPE_NULL;
    }

    bool is_string() const
    {
        return is_string(m_type);
    }

    static bool is_string(enum_field_types type)
    {
        switch (type)
        {
        case MYSQL_TYPE_BLOB:
        case MYSQL_TYPE_LONG_BLOB:
        case MYSQL_TYPE_MEDIUM_BLOB:
        case MYSQL_TYPE_STRING:
        case MYSQL_TYPE_TINY_BLOB:
        case MYSQL_TYPE_VARCHAR:
        case MYSQL_TYPE_VAR_STRING:
            return true;

        // These, although returned as length-encoded strings, also in the case of
        // a binary resultset row, are not are not considered to be strings from the
        // perspective of masking.
        case MYSQL_TYPE_BIT:
        case MYSQL_TYPE_DECIMAL:
        case MYSQL_TYPE_ENUM:
        case MYSQL_TYPE_GEOMETRY:
        case MYSQL_TYPE_NEWDECIMAL:
        case MYSQL_TYPE_SET:
            return false;

        default:
            // Nothing else is considered to be strings even though, in the case of
            // a textual resultset, that's what they all are.
            return false;
        }
    }

protected:
    enum_field_types m_type;
private:
    uint8_t* m_pData;
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
class CQRTextResultsetRowIterator : public std::iterator<std::forward_iterator_tag
                                                         , CQRTextResultsetValue
                                                         , std::ptrdiff_t
                                                         , CQRTextResultsetValue*
                                                         , CQRTextResultsetValue>
{
public:
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
        LEncString s(&m_pData);
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
class CQRBinaryResultsetRowIterator : public std::iterator<std::forward_iterator_tag
                                                           , CQRBinaryResultsetValue
                                                           , std::ptrdiff_t
                                                           , CQRBinaryResultsetValue*
                                                           , CQRBinaryResultsetValue>
{
public:
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
                LEncString s(&m_pData);     // Advance m_pData to the byte following the string.
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
 * @class ComQueryResponse
 *
 * An instance of this class represents the response to a @c ComQuery.
 */
class ComQueryResponse : public ComPacket
{
public:
    ComQueryResponse(const ComPacket& com_packet)
        : ComPacket(com_packet)
        , m_nFields(payload())
    {
    }

    uint64_t nFields() const
    {
        return m_nFields;
    }

private:
    LEncInt m_nFields;
};
}
