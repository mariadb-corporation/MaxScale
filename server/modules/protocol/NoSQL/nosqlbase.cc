/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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

#include "nosqlbase.hh"
#include "nosqlcommand.hh"

using namespace std;

namespace nosql
{

//
// Error classes
//
void Exception::append_write_error(ArrayBuilder& write_errors, int index) const
{
    DocumentBuilder write_error;
    write_error.append(kvp(key::INDEX, index));
    write_error.append(kvp(key::CODE, m_code));
    write_error.append(kvp(key::ERRMSG, what()));

    write_errors.append(write_error.extract());
}

NoError::NoError(int32_t n)
    : m_n(n)
{
}

NoError::NoError(int32_t n, bool updated_existing)
    : m_n(n)
    , m_updated_existing(updated_existing)
{
}

NoError::NoError(unique_ptr<Id>&& sUpserted)
    : m_n(1)
    , m_updated_existing(false)
    , m_sUpserted(std::move(sUpserted))
{
}

void NoError::populate(nosql::DocumentBuilder& doc)
{
    nosql::DocumentBuilder writeConcern;
    writeConcern.append(kvp(key::W, 1));
    writeConcern.append(kvp(key::WTIMEOUT, 0));

    if (m_n != -1)
    {
        doc.append(kvp(key::N, m_n));
    }

    if (m_updated_existing)
    {
        doc.append(kvp(key::UPDATED_EXISTING, m_updated_existing));
    }

    if (m_sUpserted)
    {
        m_sUpserted->append(doc, key::UPSERTED);
    }

    doc.append(kvp(key::SYNC_MILLIS, 0));
    doc.append(kvp(key::WRITTEN_TO, bsoncxx::types::b_null()));
    doc.append(kvp(key::WRITE_CONCERN, writeConcern.extract()));
    doc.append(kvp(key::ERR, bsoncxx::types::b_null()));
}

GWBUF* SoftError::create_response(const Command& command) const
{
    DocumentBuilder doc;
    create_response(command, doc);

    return command.create_response(doc.extract(), Command::IsError::YES);
}

void SoftError::create_response(const Command& command, DocumentBuilder& doc) const
{
    doc.append(kvp(key::OK, 0));
    if (command.response_kind() == Command::ResponseKind::REPLY)
    {
        // TODO: Turning on the error bit in the OP_REPLY is not sufficient, but "$err"
        // TODO: must be set as well. Figure out why, because it should not be needed.
        doc.append(kvp("$err", what()));
    }
    doc.append(kvp(key::ERRMSG, what()));
    doc.append(kvp(key::CODE, m_code));
    doc.append(kvp(key::CODE_NAME, nosql::error::name(m_code)));
}

void ConcreteLastError::populate(DocumentBuilder& doc)
{
    doc.append(nosql::kvp(nosql::key::ERR, m_err));
    doc.append(nosql::kvp(nosql::key::CODE, m_code));
    doc.append(nosql::kvp(nosql::key::CODE_NAME, nosql::error::name(m_code)));
}

unique_ptr<LastError> SoftError::create_last_error() const
{
    return std::make_unique<ConcreteLastError>(what(), m_code);
}

GWBUF* HardError::create_response(const Command& command) const
{
    DocumentBuilder doc;
    create_response(command, doc);

    return command.create_response(doc.extract(), Command::IsError::YES);
}

void HardError::create_response(const Command&, DocumentBuilder& doc) const
{
    doc.append(kvp("$err", what()));
    doc.append(kvp(key::CODE, m_code));
}

unique_ptr<LastError> HardError::create_last_error() const
{
    return std::make_unique<ConcreteLastError>(what(), m_code);
}

MariaDBError::MariaDBError(const ComERR& err)
    : Exception("Protocol command failed due to MariaDB error.", error::COMMAND_FAILED)
    , m_mariadb_code(err.code())
    , m_mariadb_message(err.message())
{
}

GWBUF* MariaDBError::create_response(const Command& command) const
{
    DocumentBuilder doc;
    create_response(command, doc);

    return command.create_response(doc.extract(), Command::IsError::YES);
}

void MariaDBError::create_response(const Command& command, DocumentBuilder& doc) const
{
    string json = command.to_json();
    string sql = command.last_statement();

    DocumentBuilder mariadb;
    mariadb.append(kvp(key::CODE, m_mariadb_code));
    mariadb.append(kvp(key::MESSAGE, m_mariadb_message));
    mariadb.append(kvp(key::COMMAND, json));
    mariadb.append(kvp(key::SQL, sql));

    doc.append(kvp("$err", what()));
    auto protocol_code = error::from_mariadb_code(m_mariadb_code);;
    doc.append(kvp(key::CODE, protocol_code));
    doc.append(kvp(key::CODE_NAME, error::name(protocol_code)));
    doc.append(kvp(key::MARIADB, mariadb.extract()));

    MXS_ERROR("Protocol command failed due to MariaDB error: "
              "json = \"%s\", code = %d, message = \"%s\", sql = \"%s\"",
              json.c_str(), m_mariadb_code, m_mariadb_message.c_str(), sql.c_str());
}

unique_ptr<LastError> MariaDBError::create_last_error() const
{
    class MariaDBLastError : public ConcreteLastError
    {
    public:
        MariaDBLastError(const string& err,
                         int32_t mariadb_code,
                         const string& mariadb_message)
            : ConcreteLastError(err, error::from_mariadb_code(mariadb_code))
            , m_mariadb_code(mariadb_code)
            , m_mariadb_message(mariadb_message)
        {
        }

        void populate(DocumentBuilder& doc) override
        {
            ConcreteLastError::populate(doc);

            DocumentBuilder mariadb;
            mariadb.append(kvp(key::CODE, m_mariadb_code));
            mariadb.append(kvp(key::MESSAGE, m_mariadb_message));

            doc.append(kvp(key::MARIADB, mariadb.extract()));
        }

    private:
        int32_t m_mariadb_code;
        string  m_mariadb_message;
    };

    return std::make_unique<ConcreteLastError>(what(), m_code);
}

//
// namespace nosql::error
//
int error::from_mariadb_code(int code)
{
    // TODO: Expand the range of used codes.

    switch (code)
    {
    case 0:
        return OK;

    default:
        return COMMAND_FAILED;
    }
}

const char* error::name(int protocol_code)
{
    switch (protocol_code)
    {
#define NOSQL_ERROR(symbol, code, name) case symbol: { return name; }
#include "nosqlerror.hh"
#undef NOSQL_ERROR

    default:
        mxb_assert(!true);
        return "";
    }
}

}

//
// namespace nosql, free functions
//
string nosql::escape_essential_chars(string&& from)
{
    auto it = from.begin();
    auto end = from.end();

    while (it != end && *it != '\'' && *it != '\\')
    {
        ++it;
    }

    if (it == end)
    {
        return from;
    }

    string to(from.begin(), it);

    if (*it == '\'')
    {
        to.push_back('\'');
    }
    else
    {
        to.push_back('\\');
    }

    to.push_back(*it++);

    while (it != end)
    {
        auto c = *it;

        switch (c)
        {
        case '\\':
            to.push_back('\\');
            break;

        case '\'':
            to.push_back('\'');
            break;

        default:
            break;
        }

        to.push_back(c);

        ++it;
    }

    return to;
}
