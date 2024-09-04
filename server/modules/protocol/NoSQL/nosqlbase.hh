/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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

#include "nosqlprotocol.hh"
#include <sstream>

// Disable bsoncxx deprecation warnings
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <bsoncxx/array/element.hpp>
#include <bsoncxx/array/view.hpp>
#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/document/element.hpp>
#include <bsoncxx/document/view.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/types.hpp>
#include <bsoncxx/types/value.hpp>
#include <maxbase/json.hh>

class GWBUF;
class ComERR;

namespace nosql
{

using bsoncxx::stdx::string_view;

inline bool operator == (const string_view& lhs, const std::string& rhs)
{
    return lhs.compare(rhs.c_str()) == 0;
}

inline bool operator != (const string_view& lhs, const std::string& rhs)
{
    return !(lhs == rhs);
}

inline bool operator == (const std::string& lhs, const string_view& rhs)
{
    return rhs.compare(lhs.c_str()) == 0;
}

inline bool operator != (const std::string& lhs, const string_view& rhs)
{
    return !(lhs == rhs);
}

inline std::string to_string(const string_view& s)
{
    return std::string(s.data(), s.length());
}

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

    int code() const
    {
        return m_code;
    }

    virtual GWBUF create_response(const Command& command) const = 0;
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

    GWBUF create_response(const Command& command) const override final;
    void create_response(const Command& command, DocumentBuilder& doc) const override final;

    std::unique_ptr<LastError> create_last_error() const override final;
};

class HardError : public Exception
{
public:
    using Exception::Exception;

    GWBUF create_response(const Command& command) const override final;
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

    GWBUF create_response(const Command& command) const override final;
    void create_response(const Command& command, DocumentBuilder& doc) const override final;

    std::unique_ptr<LastError> create_last_error() const override final;

private:
    void create_authorization_error(const Command& command, DocumentBuilder& doc) const;

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

/**
 * Checks whether a string is a valid database name.
 *
 * @param name  The string to check.
 *
 * @return True, if the name is valid.
 */
bool is_valid_database_name(const std::string& name);


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


std::string element_to_value(const bsoncxx::types::bson_value::view& x,
                             ValueFor value_for,
                             const std::string& op = "");

inline std::string element_to_value(const bsoncxx::document::element& x,
                                    ValueFor value_for,
                                    const std::string& op = "")
{
    return element_to_value(x.get_value(), value_for, op);
}

inline std::string element_to_value(const bsoncxx::array::element& x,
                                    ValueFor value_for,
                                    const std::string& op = "")
{
    return element_to_value(x.get_value(), value_for, op);
}

std::string element_to_string(const bsoncxx::types::bson_value::view& x);

inline std::string element_to_string(const bsoncxx::document::element& x)
{
    return element_to_string(x.get_value());
}

inline std::string element_to_string(const bsoncxx::array::element& x)
{
    return element_to_string(x.get_value());
}

mxb::Json bson_to_json(const bsoncxx::types::value& x);

enum class Conversion
{
    STRICT,
    RELAXED
};

template<class T>
bool bson_view_as(const bsoncxx::types::bson_value::view& view,
                  Conversion conversion,
                  T* pT);

template<class T>
bool bson_view_as(const bsoncxx::types::bson_value::view& view, T* pT)
{
    return bson_view_as(view, Conversion::STRICT, pT);
}

template<>
bool bson_view_as(const bsoncxx::types::bson_value::view& view,
                  Conversion conversion,
                  double* pT);

template<>
bool bson_view_as(const bsoncxx::types::bson_value::view& view,
                  Conversion conversion,
                  int32_t* pT);

template<>
bool bson_view_as(const bsoncxx::types::bson_value::view& view,
                  Conversion conversion,
                  std::string* pT);

template<class T>
T bson_view_as(const std::string& command,
               const char* zKey,
               const bsoncxx::types::bson_value::view& view,
               int error_code,
               Conversion conversion = Conversion::STRICT);

template<class T>
T bson_view_as(const std::string& command,
               const char* zKey,
               const bsoncxx::types::bson_value::view& view,
               Conversion conversion = Conversion::STRICT)
{
    return bson_view_as<T>(command, zKey, view, error::TYPE_MISMATCH, conversion);
}

template<>
bsoncxx::document::view bson_view_as<bsoncxx::document::view>(const std::string& command,
                                                              const char* zKey,
                                                              const bsoncxx::types::bson_value::view& view,
                                                              int error_code,
                                                              Conversion conversion);

template<>
bsoncxx::array::view bson_view_as<bsoncxx::array::view>(const std::string& command,
                                                        const char* zKey,
                                                        const bsoncxx::types::bson_value::view& view,
                                                        int error_code,
                                                        Conversion conversion);

template<>
std::string bson_view_as<std::string>(const std::string& command,
                                      const char* zKey,
                                      const bsoncxx::types::bson_value::view& view,
                                      int error_code,
                                      Conversion conversion);

template<>
std::string_view bson_view_as<std::string_view>(const std::string& command,
                                                const char* zKey,
                                                const bsoncxx::types::bson_value::view& view,
                                                int error_code,
                                                Conversion conversion);

template<>
int64_t bson_view_as<int64_t>(const std::string& command,
                              const char* zKey,
                              const bsoncxx::types::bson_value::view& view,
                              int error_code,
                              Conversion conversion);

template<>
int32_t bson_view_as<int32_t>(const std::string& command,
                              const char* zKey,
                              const bsoncxx::types::bson_value::view& view,
                              int error_code,
                              Conversion conversion);
template<>
bool bson_view_as<bool>(const std::string& command,
                        const char* zKey,
                        const bsoncxx::types::bson_value::view& view,
                        int error_code,
                        Conversion conversion);

template<>
bsoncxx::types::b_binary bson_view_as<bsoncxx::types::b_binary>(const std::string& command,
                                                                const char* zKey,
                                                                const bsoncxx::types::bson_value::view& view,
                                                                int error_code,
                                                                Conversion conversion);


template<class T>
inline bool element_as(const bsoncxx::document::element& element,
                       Conversion conversion,
                       T* pT)
{
    return bson_view_as(element.get_value(), conversion, pT);
}

template<class T>
inline bool element_as(const bsoncxx::document::element& element, T* pT)
{
    return element_as(element, Conversion::STRICT, pT);
}

template<class T>
inline T element_as(const std::string& command,
                    const char* zKey,
                    const bsoncxx::document::element& element,
                    int error_code,
                    Conversion conversion = Conversion::STRICT)
{
    return bson_view_as<T>(command, zKey, element.get_value(), error_code, conversion);
}

template<class T>
inline T element_as(const std::string& command,
                    const char* zKey,
                    const bsoncxx::document::element& element,
                    Conversion conversion = Conversion::STRICT)
{
    return element_as<T>(command, zKey, element, error::TYPE_MISMATCH, conversion);
}

template<class Type>
bool optional(const std::string& command,
              const bsoncxx::document::view& doc,
              const char* zKey,
              Type* pElement,
              int error_code,
              Conversion conversion = Conversion::STRICT)
{
    bool rv = false;

    auto element = doc[zKey];

    if (element)
    {
        *pElement = element_as<Type>(command, zKey, element, error_code, conversion);
        rv = true;
    }

    return rv;
}

template<class Type>
bool optional(const std::string& command,
              const bsoncxx::document::view& doc,
              const char* zKey,
              Type* pElement,
              Conversion conversion = Conversion::STRICT)
{
    return optional(command, doc, zKey, pElement, error::TYPE_MISMATCH, conversion);
}

}
