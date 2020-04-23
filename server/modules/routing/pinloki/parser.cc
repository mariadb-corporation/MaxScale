/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-05
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "parser.hh"

#include <boost/fusion/adapted/struct/adapt_struct.hpp>
#include <boost/fusion/include/adapt_struct.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/spirit/home/x3.hpp>
#include <boost/spirit/home/x3/support/ast/position_tagged.hpp>
#include <boost/spirit/home/x3/support/ast/variant.hpp>
#include <boost/spirit/home/x3/support/utility/annotate_on_success.hpp>
#include <boost/spirit/home/x3/support/utility/error_reporting.hpp>

using namespace boost::spirit;

namespace
{

enum class Slave
{
    START,
    STOP,
    RESET,
};

enum class Logs
{
    FLUSH,
    PURGE,
};

enum ChangeMasterType
{
    MASTER_HOST,
    MASTER_PORT,
    MASTER_USER,
    MASTER_PASSWORD,
    MASTER_USE_GTID,
    MASTER_SSL,
    MASTER_SSL_CA,
    MASTER_SSL_CAPATH,
    MASTER_SSL_CERT,
    MASTER_SSL_CRL,
    MASTER_SSL_CRLPATH,
    MASTER_SSL_KEY,
    MASTER_SSL_CIPHER,
    MASTER_SSL_VERIFY_SERVER_CERT,
};

// SLAVE command mapping
struct SlaveSymbols : x3::symbols<Slave>
{
    SlaveSymbols()
    {
        add("START", Slave::START);
        add("STOP", Slave::STOP);
        add("RESET", Slave::RESET);
    }
} slave_sym;

// LOGS command mapping
struct LogsSymbols : x3::symbols<Logs>
{
    LogsSymbols()
    {
        add("FLUSH", Logs::FLUSH);
        add("PURGE", Logs::PURGE);
    }
} logs_sym;

// CHANGE MASTER argument types
struct ChangeMasterSymbols : x3::symbols<ChangeMasterType>
{
    ChangeMasterSymbols()
    {
        add("MASTER_HOST", MASTER_HOST);
        add("MASTER_PORT", MASTER_PORT);
        add("MASTER_USER", MASTER_USER);
        add("MASTER_PASSWORD", MASTER_PASSWORD);
        add("MASTER_USE_GTID", MASTER_USE_GTID);
        add("MASTER_SSL", MASTER_SSL);
        add("MASTER_SSL_CA", MASTER_SSL_CA);
        add("MASTER_SSL_CAPATH", MASTER_SSL_CAPATH);
        add("MASTER_SSL_CERT", MASTER_SSL_CERT);
        add("MASTER_SSL_CRL", MASTER_SSL_CRL);
        add("MASTER_SSL_CRLPATH", MASTER_SSL_CRLPATH);
        add("MASTER_SSL_KEY", MASTER_SSL_KEY);
        add("MASTER_SSL_CIPHER", MASTER_SSL_CIPHER);
        add("MASTER_SSL_VERIFY_SERVER_CERT", MASTER_SSL_VERIFY_SERVER_CERT);
    }
} change_master_sym;

//
// Concrete types that the result consists of
//

// An individual field in a SELECT statement
using Field = x3::variant<int, double, std::string>;

// A key-value with variant values
struct Variable
{
    std::string key;
    Field       value;
};

// A key-value with a limited set of accepted keys
struct ChangeMasterVariable
{
    ChangeMasterType key;
    Field            value;
};

// SELECT is a list of fields
struct Select
{
    std::vector<Field> values;
};

// SET is a list of key-value pairs
struct Set
{
    std::vector<Variable> values;
};

struct ChangeMaster
{
    std::vector<ChangeMasterVariable> values;
};

// The root type that is returned as the result of parsing
using Command = x3::variant<Select, Set, ChangeMaster, Slave, Logs>;

// Error handler that the rule types must inherit from, allows pretty-printing of errors
struct error_handler
{
    template<typename Iterator, typename Exception, typename Context>
    x3::error_handler_result on_error(Iterator& first, Iterator const& last,
                                      Exception const& x, Context const& context)
    {
        auto& error_handler = x3::get<x3::error_handler_tag>(context).get();
        std::string message = "Error! Expecting `" + x.which() + "`:";
        error_handler(x.where(), message);
        return x3::error_handler_result::fail;
    }
};


/**
 * Declare a rule with an attribute
 *
 * @param id        Rule ID, declared as a variable
 * @param desc      Rule type description
 * @param attr_type Rule attribute (i.e. return value)
 */
#define DECLARE_ATTR_RULE(id, desc, attr_type) \
    struct id : public error_handler {}; \
    const x3::rule<struct id, attr_type> id = desc

/**
 * Declare a rule
 *
 * The rule attribute is deduced using the rule definition.
 *
 * @param id        Rule ID, declared as a variable
 * @param desc      Rule type description
 */
#define DECLARE_RULE(id, desc) \
    struct id : public error_handler {}; \
    const x3::rule<struct id> id = desc

DECLARE_RULE(eq, "=");
DECLARE_RULE(number, "number");
DECLARE_ATTR_RULE(str, "string", std::string);
DECLARE_ATTR_RULE(sq_str, "string", std::string);
DECLARE_ATTR_RULE(dq_str, "single-quoted string", std::string);
DECLARE_ATTR_RULE(field, "field", Field);
DECLARE_ATTR_RULE(variable, "key-value", Variable);
DECLARE_ATTR_RULE(change_master_variable, "key-value", ChangeMasterVariable);
DECLARE_ATTR_RULE(select, "select", Select);
DECLARE_ATTR_RULE(set, "set", Set);
DECLARE_ATTR_RULE(change_master, "change master", ChangeMaster);
DECLARE_ATTR_RULE(slave, "slave", Slave);
DECLARE_ATTR_RULE(logs, "logs", Logs);
DECLARE_ATTR_RULE(grammar, "grammar", Command);

//
// The actual grammar part
//

// Basic types
const auto eq_def = x3::omit['='];
const auto number_def = x3::int_ | x3::double_ | (x3::lit("0x") >> x3::int_);
const auto str_def = x3::lexeme[+(x3::ascii::alnum | x3::char_("_@."))];
const auto sq_str_def = x3::lexeme[x3::lit('\'') > +(x3::char_ - '\'') > x3::lit('\'')];
const auto dq_str_def = x3::lexeme[x3::lit('"') > +(x3::char_ - '"') > x3::lit('"')];

// Generic fields and key-values
const auto field_def = str | sq_str | dq_str | number;
const auto variable_def = str > eq > field;

// SET and SELECT commands
const auto select_def = x3::lit("SELECT") > field % ',';
const auto set_def = x3::lit("SET") > (variable % ',');

// CHANGE MASTER TO, only accepts a limited set of keys
const auto change_master_variable_def = change_master_sym > eq > field;
const auto change_master_def = x3::lit("CHANGE") > x3::lit("MASTER") > x3::lit("TO")
    > (change_master_variable % ',');

// START SLAVE et al. and FLUSH and PURGE LOGS
const auto slave_def = slave_sym > "SLAVE";
const auto logs_def = logs_sym > "LOGS";

// The complete grammar, case insensitive
const auto grammar_def = x3::no_case[
    select
    | set
    | change_master
    | slave
    | logs];

// Boost magic that combines the rule declarations and definitions (definitions _must_ end in a _def suffix)
BOOST_SPIRIT_DEFINE(number, str, sq_str, dq_str, field, variable, select, set,
                    change_master_variable, change_master, slave, logs, grammar, eq);


// The visitor class that does the final processing of the result
struct ResultVisitor : public boost::static_visitor<>
{
    ResultVisitor(parser::Handler* handler)
        : m_handler(handler)
    {
    }

    void operator()(Select& s)
    {
        std::vector<std::string> values;

        for (const auto& a : s.values)
        {
            values.push_back(get<std::string>(a));
        }

        m_handler->select(values);
    }

    void operator()(Set& s)
    {
        for (const auto& a : s.values)
        {
            m_handler->set(a.key, get<std::string>(a.value));
        }
    }

    void operator()(ChangeMaster& s)
    {
        parser::MasterConfig master;

        for (const auto& a : s.values)
        {
            switch (a.key)
            {
            case MASTER_HOST:
                master.host = get<std::string>(a.value);
                break;

            case MASTER_PORT:
                master.port = get<int>(a.value);
                break;

            case MASTER_USER:
                master.user = get<std::string>(a.value);
                break;

            case MASTER_PASSWORD:
                master.password = get<std::string>(a.value);
                break;

            case MASTER_USE_GTID:
                master.use_gtid = strcasecmp(get<std::string>(a.value).c_str(), "slave_pos") == 0;
                break;

            case MASTER_SSL:
                master.ssl = get<int>(a.value);
                break;

            case MASTER_SSL_CA:
                master.ssl_ca = get<std::string>(a.value);
                break;

            case MASTER_SSL_CAPATH:
                master.ssl_capath = get<std::string>(a.value);
                break;

            case MASTER_SSL_CERT:
                master.ssl_cert = get<std::string>(a.value);
                break;

            case MASTER_SSL_CRL:
                master.ssl_crl = get<std::string>(a.value);
                break;

            case MASTER_SSL_CRLPATH:
                master.ssl_crlpath = get<std::string>(a.value);
                break;

            case MASTER_SSL_KEY:
                master.ssl_key = get<std::string>(a.value);
                break;

            case MASTER_SSL_CIPHER:
                master.ssl_cipher = get<std::string>(a.value);
                break;

            case MASTER_SSL_VERIFY_SERVER_CERT:
                master.ssl_verify_server_cert = get<int>(a.value);
                break;
            }
        }

        m_handler->change_master_to(master);
    }

    void operator()(Slave& s)
    {
        switch (s)
        {
        case Slave::START:
            m_handler->start_slave();
            break;

        case Slave::STOP:
            m_handler->stop_slave();
            break;

        case Slave::RESET:
            m_handler->reset_slave();
            break;
        }
    }

    void operator()(Logs& s)
    {
        switch (s)
        {
        case Logs::FLUSH:
            m_handler->flush_logs();
            break;

        case Logs::PURGE:
            m_handler->purge_logs();
            break;
        }
    }

private:

    // This is needed to convert variant types
    template<class T>
    struct ToTypeVisitor : public boost::static_visitor<>
    {
        template<class V>
        void operator()(V& v)
        {
            value = boost::lexical_cast<T>(v);
        }

        T value;
    };

    // Helper for extracting variant types
    template<class T, class V>
    T get(V v)
    {
        ToTypeVisitor<T> visitor;
        boost::apply_visitor(visitor, v);
        return visitor.value;
    }

    parser::Handler* m_handler;
};
}

// Boost magic that automatically maps parse results to member variables. Needs to be done outside of the
// anonymous namespace (for some reason).
BOOST_FUSION_ADAPT_STRUCT(Variable, key, value);
BOOST_FUSION_ADAPT_STRUCT(ChangeMasterVariable, key, value);
BOOST_FUSION_ADAPT_STRUCT(Select, values);
BOOST_FUSION_ADAPT_STRUCT(Set, values);
BOOST_FUSION_ADAPT_STRUCT(ChangeMaster, values);

namespace parser
{
void parse(const std::string& line, Handler* handler)
{
    auto start = line.begin();
    auto end = line.end();
    Command cmd;
    bool rv = false;
    std::ostringstream err;

    // The x3::with applies the error handler to the grammar, required to enable error printing
    auto err_handler = x3::error_handler<decltype(start)>(start, end, err);
    auto parser = x3::with<x3::error_handler_tag>(std::ref(err_handler))[grammar];

    try
    {
        rv = x3::phrase_parse(start, end, parser, x3::ascii::space, cmd);
    }
    catch (const std::exception& e)
    {
        err << e.what();
    }

    if (rv && start == end)
    {
        ResultVisitor visitor(handler);
        boost::apply_visitor(visitor, cmd);
    }
    else
    {
        handler->error(err.str());
    }
}
}
