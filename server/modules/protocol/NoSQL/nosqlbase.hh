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
#pragma once

#include "nosqlprotocol.hh"
#include <sstream>
#include <bsoncxx/array/element.hpp>
#include <bsoncxx/array/view.hpp>
#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/document/element.hpp>
#include <bsoncxx/document/view.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/types.hpp>

class GWBUF;
class ComERR;

namespace nosql
{

using bsoncxx::stdx::string_view;

using DocumentBuilder = bsoncxx::builder::basic::document;
using ArrayBuilder = bsoncxx::builder::basic::array;
using bsoncxx::builder::basic::kvp;

class Command;

namespace error
{

#define NOSQL_ERROR(symbol, code, name) const int symbol = code;
#include "nosqlerror.hh"
#undef NOSQL_ERROR

int from_mariadb_code(int code);

const char* name(int code);

}

class LastError
{
public:
    virtual ~LastError() {}

    virtual void populate(DocumentBuilder& doc) = 0;
};

class ConcreteLastError: public LastError
{
public:
    ConcreteLastError(const std::string& err, int32_t code)
        : m_err(err)
        , m_code(code)
    {
    }

    void populate(DocumentBuilder& doc) override;

private:
    std::string m_err;
    int32_t     m_code;
    std::string m_code_name;
};

class NoError : public LastError
{
public:
    class Id
    {
    public:
        virtual ~Id() {};

        virtual std::string to_string() const = 0;

        virtual void append(DocumentBuilder& doc, const std::string& key) const = 0;
    };

    const static bsoncxx::oid null_oid;

    NoError(int32_t n = 0);
    NoError(int32_t n, bool updated_existing);
    NoError(std::unique_ptr<Id>&& sUpserted);

    void populate(DocumentBuilder& doc) override;

private:
    int32_t             m_n { -1 };
    bool                m_updated_existing { false };
    std::unique_ptr<Id> m_sUpserted;
};

class Exception : public std::runtime_error
{
public:
    Exception(const std::string& message, int code)
        : std::runtime_error(message)
        , m_code(code)
    {
    }

    virtual GWBUF* create_response(const Command& command) const = 0;
    virtual void create_response(const Command& command, DocumentBuilder& doc) const = 0;
    void append_write_error(ArrayBuilder& write_errors, int index) const;

    virtual std::unique_ptr<LastError> create_last_error() const = 0;

protected:
    int m_code;
};

class SoftError : public Exception
{
public:
    using Exception::Exception;

    GWBUF* create_response(const Command& command) const override final;
    void create_response(const Command& command, DocumentBuilder& doc) const override final;

    std::unique_ptr<LastError> create_last_error() const override final;
};

class HardError : public Exception
{
public:
    using Exception::Exception;

    GWBUF* create_response(const Command& command) const override final;
    void create_response(const Command& command, DocumentBuilder& doc) const override final;

    std::unique_ptr<LastError> create_last_error() const override final;
};

class MariaDBError : public Exception
{
public:
    MariaDBError(const ComERR& err);

    int code() const
    {
        return m_mariadb_code;
    }

    const std::string& message() const
    {
        return m_mariadb_message;
    }

    GWBUF* create_response(const Command& command) const override final;
    void create_response(const Command& command, DocumentBuilder& doc) const override final;

    std::unique_ptr<LastError> create_last_error() const override final;

private:
    int         m_mariadb_code;
    std::string m_mariadb_message;
};

/**
 * Escape the characters \ and '.
 *
 * @param from  The string to escape.
 *
 * @return The same string with \ and ' escaped.
 */
std::string escape_essential_chars(std::string&& from);

inline std::string escape_essential_chars(const string_view& sv)
{
    return escape_essential_chars(std::string(sv.data(), sv.length()));
}

enum class ValueFor
{
    JSON,
    JSON_NESTED,
    SQL
};

inline void double_to_string(double d, std::ostream& os)
{
    // printf("%.20g\n", -std::numeric_limits<double>::max()) => "-1.7976931348623157081e+308"
    char buffer[28];

    sprintf(buffer, "%.20g", d);

    os << buffer;

    if (strpbrk(buffer, ".e") == nullptr)
    {
        // No decimal point, add ".0" to prevent this number from being an integer.
        os << ".0";
    }
}

inline std::string double_to_string(double d)
{
    std::ostringstream ss;
    double_to_string(d, ss);
    return ss.str();
}


template<class document_element_or_array_item>
std::string element_to_value(const document_element_or_array_item& x,
                             ValueFor value_for,
                             const std::string& op = "")
{
    std::ostringstream ss;

    switch (x.type())
    {
    case bsoncxx::type::k_double:
        double_to_string(x.get_double(), ss);
        break;

    case bsoncxx::type::k_utf8:
        {
            const auto& utf8 = x.get_utf8();
            string_view s(utf8.value.data(), utf8.value.size());

            switch (value_for)
            {
            case ValueFor::JSON:
                ss << "'\"" << s << "\"'";
                break;

            case ValueFor::JSON_NESTED:
            case ValueFor::SQL:
                ss << "\"" << s << "\"";
            }
        }
        break;

    case bsoncxx::type::k_int32:
        ss << x.get_int32();
        break;

    case bsoncxx::type::k_int64:
        ss << x.get_int64();
        break;

    case bsoncxx::type::k_binary:
        {
            auto b = x.get_binary();

            string_view s(reinterpret_cast<const char*>(b.bytes), b.size);

            ss << "'" << s << "'";
        }
        break;

    case bsoncxx::type::k_bool:
        ss << x.get_bool();
        break;

    case bsoncxx::type::k_date:
        ss << x.get_date();
        break;

    case bsoncxx::type::k_array:
        {
            ss << "JSON_ARRAY(";

            bsoncxx::array::view a = x.get_array();

            bool first = true;
            for (auto element : a)
            {
                if (first)
                {
                    first = false;
                }
                else
                {
                    ss << ", ";
                }

                ss << element_to_value(element, ValueFor::JSON_NESTED, op);
            }

            ss << ")";
        }
        break;

    case bsoncxx::type::k_document:
        {
            ss << "JSON_OBJECT(";

            bsoncxx::document::view d = x.get_document();

            bool first = true;
            for (auto element : d)
            {
                if (first)
                {
                    first = false;
                }
                else
                {
                    ss << ", ";
                }

                ss << "\"" << element.key() << "\", " << element_to_value(element, ValueFor::JSON_NESTED, op);
            }

            ss << ")";
        }
        break;

    case bsoncxx::type::k_null:
        switch (value_for)
        {
        case ValueFor::JSON:
        case ValueFor::JSON_NESTED:
            ss << "null";
            break;

        case ValueFor::SQL:
            ss << "'null'";
        }
        break;

    case bsoncxx::type::k_regex:
        {
            std::ostringstream ss2;

            auto r = x.get_regex();
            if (r.options.length() != 0)
            {
                ss2 << "(?" << r.options << ")";
            }

            ss2 << r.regex;

            ss << "REGEXP '" << escape_essential_chars(ss2.str()) << "'";
        }
        break;

    case bsoncxx::type::k_minkey:
        ss << std::numeric_limits<int64_t>::min();
        break;

    case bsoncxx::type::k_maxkey:
        ss << std::numeric_limits<int64_t>::max();
        break;

    case bsoncxx::type::k_code:
        ss << "'" << x.get_code().code << "'";
        break;

    case bsoncxx::type::k_undefined:
        throw SoftError("cannot compare to undefined", error::BAD_VALUE);

    default:
        {
            ss << "cannot convert a " << bsoncxx::to_string(x.type()) << " to a value for comparison";

            throw nosql::SoftError(ss.str(), nosql::error::BAD_VALUE);
        }
    }

    return ss.str();
}

template<class document_element_or_array_item>
std::string element_to_string(const document_element_or_array_item& x)
{
    std::ostringstream ss;

    switch (x.type())
    {
    case bsoncxx::type::k_array:
        {
            bool first = true;
            ss << "[";
            bsoncxx::array::view array = x.get_array();
            for (const auto& item : array)
            {
                if (first)
                {
                    first = false;
                }
                else
                {
                    ss << ", ";
                }

                ss << element_to_string(item);
            }
            ss << "]";
        }
        break;

    case bsoncxx::type::k_bool:
        ss << x.get_bool();
        break;

    case bsoncxx::type::k_code:
        ss << x.get_code().code;
        break;

    case bsoncxx::type::k_date:
        ss << x.get_date();
        break;

    case bsoncxx::type::k_decimal128:
        ss << x.get_decimal128().value.to_string();
        break;

    case bsoncxx::type::k_document:
        ss << escape_essential_chars(std::move(bsoncxx::to_json(x.get_document())));
        break;

    case bsoncxx::type::k_double:
        ss << element_to_value(x, ValueFor::JSON);
        break;

    case bsoncxx::type::k_int32:
        ss << x.get_int32();
        break;

    case bsoncxx::type::k_int64:
        ss << x.get_int64();
        break;

    case bsoncxx::type::k_null:
        ss << "null";
        break;

    case bsoncxx::type::k_oid:
        ss << "{\"$oid\":\"" << x.get_oid().value.to_string() << "\"}";
        break;

    case bsoncxx::type::k_regex:
        ss << x.get_regex().regex;
        break;

    case bsoncxx::type::k_symbol:
        ss << x.get_symbol().symbol;
        break;

    case bsoncxx::type::k_utf8:
        {
            const auto& view = x.get_utf8().value;
            std::string value(view.data(), view.length());
            ss << escape_essential_chars(std::move(value));
        }
        break;

    case bsoncxx::type::k_minkey:
        ss << "{\"$minKey\":1}";
        break;

    case bsoncxx::type::k_maxkey:
        ss << "{\"$maxKey\":1}";
        break;

    case bsoncxx::type::k_undefined:
        throw SoftError("cannot compare to undefined", error::BAD_VALUE);
        break;

    case bsoncxx::type::k_binary:
    case bsoncxx::type::k_codewscope:
    case bsoncxx::type::k_dbpointer:
    case bsoncxx::type::k_timestamp:
        {
            ss << "A " << bsoncxx::to_string(x.type()) << " cannot be converted to a string.";
            throw SoftError(ss.str(), error::BAD_VALUE);
        }
        break;
    }

    return ss.str();
}

enum class Conversion
{
    STRICT,
    RELAXED
};

template<class T>
bool element_as(const bsoncxx::document::element& element,
                Conversion conversion,
                T* pT);

template<class T>
inline bool element_as(const bsoncxx::document::element& element, T* pT)
{
    return element_as(element, Conversion::STRICT, pT);
}

template<>
bool element_as(const bsoncxx::document::element& element,
                Conversion conversion,
                double* pT);

template<class T>
T element_as(const std::string& command,
             const char* zKey,
             const bsoncxx::document::element& element,
             int error_code,
             Conversion conversion = Conversion::STRICT);

template<class T>
T element_as(const std::string& command,
             const char* zKey,
             const bsoncxx::document::element& element,
             Conversion conversion = Conversion::STRICT)
{
    return element_as<T>(command, zKey, element, error::TYPE_MISMATCH, conversion);
}

template<>
bsoncxx::document::view element_as<bsoncxx::document::view>(const std::string& command,
                                                            const char* zKey,
                                                            const bsoncxx::document::element& element,
                                                            int error_code,
                                                            Conversion conversion);

template<>
bsoncxx::array::view element_as<bsoncxx::array::view>(const std::string& command,
                                                      const char* zKey,
                                                      const bsoncxx::document::element& element,
                                                      int error_code,
                                                      Conversion conversion);

template<>
std::string element_as<std::string>(const std::string& command,
                                    const char* zKey,
                                    const bsoncxx::document::element& element,
                                    int error_code,
                                    Conversion conversion);

template<>
int64_t element_as<int64_t>(const std::string& command,
                            const char* zKey,
                            const bsoncxx::document::element& element,
                            int error_code,
                            Conversion conversion);

template<>
int32_t element_as<int32_t>(const std::string& command,
                            const char* zKey,
                            const bsoncxx::document::element& element,
                            int error_code,
                            Conversion conversion);
template<>
bool element_as<bool>(const std::string& command,
                      const char* zKey,
                      const bsoncxx::document::element& element,
                      int error_code,
                      Conversion conversion);

}
