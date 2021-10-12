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

#define BOOST_ERROR_CODE_HEADER_ONLY 1

#include "parser.hh"

// This prevents automatic rule name deduction, helps keep the error messages cleaner.
#define BOOST_SPIRIT_X3_NO_RTTI

#include <boost/fusion/adapted/struct/adapt_struct.hpp>
#include <boost/fusion/include/adapt_struct.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/spirit/home/x3.hpp>
#include <boost/spirit/home/x3/support/ast/position_tagged.hpp>
#include <boost/spirit/home/x3/support/ast/variant.hpp>
#include <boost/spirit/home/x3/support/utility/annotate_on_success.hpp>
#include <boost/spirit/home/x3/support/utility/error_reporting.hpp>

#include <maxbase/assert.h>

using namespace boost::spirit;
using CMT = pinloki::ChangeMasterType;

namespace
{

constexpr std::array<const char*, int(CMT::END)> master_type_strs
{
    "MASTER_HOST",
    "MASTER_PORT",
    "MASTER_USER",
    "MASTER_PASSWORD",
    "MASTER_USE_GTID",
    "MASTER_SSL",
    "MASTER_SSL_CA",
    "MASTER_SSL_CAPATH",
    "MASTER_SSL_CERT",
    "MASTER_SSL_CRL",
    "MASTER_SSL_CRLPATH",
    "MASTER_SSL_KEY",
    "MASTER_SSL_CIPHER",
    "MASTER_SSL_VERIFY_SERVER_CERT",
    "MASTER_LOG_FILE",
    "MASTER_LOG_POS",
    "RELAY_LOG_FILE",
    "RELAY_LOG_POS",
    "MASTER_HEARTBEAT_PERIOD",
    "MASTER_BIND",
    "MASTER_CONNECT_RETRY",
    "MASTER_DELAY",
    "IGNORE_SERVER_IDS",
    "DO_DOMAIN_IDS",
    "IGNORE_DOMAIN_IDS"
};

static_assert(master_type_strs.size() == size_t(CMT::END), "check master_type_strs");
}

namespace pinloki
{

std::string to_string(CMT type)
{
    size_t index = size_t(type);
    if (index >= master_type_strs.size())
    {
        return "UNKNOWN";
    }

    return master_type_strs[index];
}
}

namespace
{

enum class Slave
{
    START,
    STOP,
    RESET,
};

enum class ShowType
{
    MASTER_STATUS,
    SLAVE_STATUS,
    ALL_SLAVES_STATUS,
    BINLOGS,
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

// CHANGE MASTER argument types
struct ChangeMasterSymbols : x3::symbols<CMT>
{
    ChangeMasterSymbols()
    {
        add(to_string(CMT::MASTER_HOST), CMT::MASTER_HOST);
        add(to_string(CMT::MASTER_PORT), CMT::MASTER_PORT);
        add(to_string(CMT::MASTER_USER), CMT::MASTER_USER);
        add(to_string(CMT::MASTER_PASSWORD), CMT::MASTER_PASSWORD);
        add(to_string(CMT::MASTER_USE_GTID), CMT::MASTER_USE_GTID);
        add(to_string(CMT::MASTER_SSL), CMT::MASTER_SSL);
        add(to_string(CMT::MASTER_SSL_CA), CMT::MASTER_SSL_CA);
        add(to_string(CMT::MASTER_SSL_CAPATH), CMT::MASTER_SSL_CAPATH);
        add(to_string(CMT::MASTER_SSL_CERT), CMT::MASTER_SSL_CERT);
        add(to_string(CMT::MASTER_SSL_CRL), CMT::MASTER_SSL_CRL);
        add(to_string(CMT::MASTER_SSL_CRLPATH), CMT::MASTER_SSL_CRLPATH);
        add(to_string(CMT::MASTER_SSL_KEY), CMT::MASTER_SSL_KEY);
        add(to_string(CMT::MASTER_SSL_CIPHER), CMT::MASTER_SSL_CIPHER);
        add(to_string(CMT::MASTER_SSL_VERIFY_SERVER_CERT), CMT::MASTER_SSL_VERIFY_SERVER_CERT);
        add(to_string(CMT::MASTER_LOG_FILE), CMT::MASTER_LOG_FILE);
        add(to_string(CMT::MASTER_LOG_POS), CMT::MASTER_LOG_POS);
        add(to_string(CMT::MASTER_BIND), CMT::MASTER_BIND);
        add(to_string(CMT::MASTER_CONNECT_RETRY), CMT::MASTER_CONNECT_RETRY);
        add(to_string(CMT::MASTER_HEARTBEAT_PERIOD), CMT::MASTER_HEARTBEAT_PERIOD);
        add(to_string(CMT::RELAY_LOG_FILE), CMT::RELAY_LOG_FILE);
        add(to_string(CMT::RELAY_LOG_POS), CMT::RELAY_LOG_POS);
        add(to_string(CMT::MASTER_DELAY), CMT::MASTER_DELAY);
        add(to_string(CMT::IGNORE_SERVER_IDS), CMT::IGNORE_SERVER_IDS);
        add(to_string(CMT::DO_DOMAIN_IDS), CMT::DO_DOMAIN_IDS);
        add(to_string(CMT::IGNORE_DOMAIN_IDS), CMT::IGNORE_DOMAIN_IDS);
    }
} change_master_sym;

//
// Concrete types that the result consists of
//

// An individual field in a SELECT statement. Having the std::string as the first value allows empty values to
// be conveniently detected during extraction.
using Field = x3::variant<std::string, int, double>;

struct SelectField
{
    Field orig_name;    // The original name of the field
    Field alias_name;   // The user-defined alias for the field
};

// A key-value with variant values
struct Variable
{
    std::string key;
    Field       value;
};

// A key-value with a limited set of accepted keys
struct ChangeMasterVariable
{
    CMT   key;
    Field value;
};

// SELECT is a list of fields
struct Select
{
    std::vector<SelectField> values;
};

// SET is a list of key-value pairs
struct Set
{
    x3::variant<Variable, std::vector<Variable>> values;
};

struct ChangeMaster
{
    std::string                       connection_name;
    std::vector<ChangeMasterVariable> values;
};

struct ShowVariables
{
    std::string like;
};

struct PurgeLogs
{
    std::string up_to;
};

struct MasterGtidWait
{
    std::string gtid;
    int         timeout = 0;
};

using Show = x3::variant<ShowType, ShowVariables>;

// The root type that is returned as the result of parsing
using Command = x3::variant<nullptr_t, Select, Set, ChangeMaster, Slave, PurgeLogs, Show, MasterGtidWait>;

// Error handler that the rule types must inherit from, allows pretty-printing of errors
struct error_handler
{
    template<typename Iterator, typename Exception, typename Context>
    x3::error_handler_result on_error(Iterator& first, Iterator const& last,
                                      Exception const& x, Context const& context)
    {
        auto& error_handler = x3::get<x3::error_handler_tag>(context).get();
        std::string message;

        if (x.which() == "undefined")
        {
            message = "Syntax error.";
        }
        else
        {
            message = "Error! Expecting `" + x.which() + "`:";
        }

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
DECLARE_ATTR_RULE(str, "string", std::string);
DECLARE_ATTR_RULE(sq_str, "string", std::string);
DECLARE_ATTR_RULE(dq_str, "single-quoted string", std::string);
DECLARE_ATTR_RULE(q_str, "quoted string", std::string);
DECLARE_ATTR_RULE(func, "function", std::string);
DECLARE_ATTR_RULE(field, "intentifier, function, string or number", Field);
DECLARE_ATTR_RULE(select_field, "field definition", SelectField);
DECLARE_ATTR_RULE(master_gtid_wait, "MASTER_GTID_WAIT", MasterGtidWait);
DECLARE_ATTR_RULE(variable, "key-value", Variable);
DECLARE_ATTR_RULE(change_master_variable, "key-value", ChangeMasterVariable);
DECLARE_ATTR_RULE(show_master, "show master", ShowType);
DECLARE_ATTR_RULE(show_slave, "show slave", ShowType);
DECLARE_ATTR_RULE(show_all_slaves, "show all slaves", ShowType);
DECLARE_ATTR_RULE(show_binlogs, "binary logs", ShowType);
DECLARE_ATTR_RULE(show_variables, "show variables", ShowVariables);
DECLARE_ATTR_RULE(show_options, "MASTER, SLAVE, BINARY LOGS or VARIABLES LIKE '...'", Show);
DECLARE_ATTR_RULE(show, "show", Show);
DECLARE_ATTR_RULE(select, "select", Select);
DECLARE_RULE(global_or_session, "GLOBAL or SESSION");
DECLARE_ATTR_RULE(set, "set", Set);
DECLARE_ATTR_RULE(set_names, "set names", Variable);
DECLARE_ATTR_RULE(change_master, "change master", ChangeMaster);
DECLARE_ATTR_RULE(slave, "slave", Slave);
DECLARE_ATTR_RULE(purge_logs, "purge logs", PurgeLogs);
DECLARE_RULE(end_of_input, "end of input");
DECLARE_ATTR_RULE(command, "command", Command);
DECLARE_ATTR_RULE(set_statement, "set_stmt", Command);
DECLARE_ATTR_RULE(grammar, "grammar", Command);

//
// The actual grammar part
//

// Basic types
const auto eq_def = x3::omit['='];
const auto str_def = x3::lexeme[+(x3::ascii::alnum | x3::char_("_@."))];
const auto func_def = str
    > x3::lit("(") > x3::omit[*(x3::ascii::char_ - ')')] > x3::lit(")")
    > x3::attr(std::string("()"));      // Must be std::string, otherwise the null character is included
const auto sq_str_def = x3::lexeme[x3::lit('\'') > *(x3::char_ - '\'') > x3::lit('\'')];
const auto dq_str_def = x3::lexeme[x3::lit('"') > *(x3::char_ - '"') > x3::lit('"')];
const auto q_str_def = sq_str | dq_str;

// Generic fields and key-values
const auto field_def = sq_str | dq_str | x3::double_ | x3::int_ | func | str;
const auto variable_def = str > eq > field;

// Field definition for SELECT statements
// NOTE: This syntax allows for a field to have a trailing "AS" constant that does nothing. Strictly speaking
//       this isn't allowed but it keeps the grammar extremely simple.
const auto select_field_def = field >> -x3::omit[x3::lit("AS")] >> -field;

// Preliminary SELECT MASTER_GTID_WAIT support. This isn't the prettiest solution but it allows testing
// without modifications to the SELECT grammar.
//
// TODO: Evaluate whether adding it to the SELECT grammar is worth it.
const auto master_gtid_wait_def = x3::lit("SELECT") > x3::lit("MASTER_GTID_WAIT")
    > x3::lit("(")
    > q_str > -(x3::lit(",") > x3::int_)
    > x3::lit(")");

// SET and SELECT commands
const auto select_def = x3::lit("SELECT") > select_field % ',' > -x3::omit[x3::lit("LIMIT") > x3::int_ % ','];

const auto set_names_def = x3::string("NAMES") > (str | q_str);
const auto global_or_session_def = -x3::omit[x3::lit("GLOBAL") | x3::lit("SESSION") | x3::lit("@@global.")];
const auto set_def = x3::lit("SET") > global_or_session > (set_names | (variable % ','));

// CHANGE MASTER TO, only accepts a limited set of keys
const auto change_master_variable_def = change_master_sym > eq > field;
const auto change_master_def = x3::lit("CHANGE") > x3::lit("MASTER") > -q_str > x3::lit("TO")
    > (change_master_variable % ',');

// START SLAVE et al. The connection_name, if any, is ignored.
const auto slave_def = slave_sym > "SLAVE" > x3::omit[-q_str];

// PURGE {BINARY | MASTER} LOGS TO '<binlog name>'
const auto purge_logs_def = x3::lit("PURGE") > (x3::lit("BINARY") | x3::lit("MASTER")) > x3::lit("LOGS")
    > x3::lit("TO") > q_str;

// SHOW commands
const auto show_master_def = x3::lit("MASTER") > x3::lit("STATUS") > x3::attr(ShowType::MASTER_STATUS);
const auto show_slave_def = x3::lit("SLAVE") > x3::lit("STATUS") > x3::attr(ShowType::SLAVE_STATUS);
const auto show_all_slaves_def = x3::lit("ALL") > x3::lit("SLAVES")
    > x3::lit("STATUS") > x3::attr(ShowType::ALL_SLAVES_STATUS);
const auto show_binlogs_def = x3::lit("BINARY") > x3::lit("LOGS") > x3::attr(ShowType::BINLOGS);
const auto show_variables_def = x3::lit("VARIABLES") > x3::lit("LIKE") > q_str;
const auto show_options_def = (show_master | show_slave | show_all_slaves
                               | show_binlogs | show_variables);
const auto show_def = x3::lit("SHOW") > show_options;
const auto end_of_input_def = x3::eoi | x3::lit(";");

const auto command_def =
    master_gtid_wait
    | select
    | set
    | change_master
    | slave
    | show
    | purge_logs;

// SET STATEMENT ... Parsed, but not used (not implemented)
const auto set_statement_def = x3::lit("SET") > x3::lit("STATEMENT")
    > x3::omit[variable % ','] > x3::lit("FOR") > command;

// The complete grammar, case insensitive
const auto grammar_def = x3::no_case[
    command
    | set_statement] > end_of_input;

// Boost magic that combines the rule declarations and definitions (definitions _must_ end in a _def suffix)
BOOST_SPIRIT_DEFINE(str, sq_str, dq_str, field, select_field, variable, select, set, eq, q_str,
                    show_master, show_slave, show_all_slaves, show_binlogs, show_variables, show, set_names,
                    global_or_session, show_options, func, master_gtid_wait,
                    change_master_variable, change_master, slave, purge_logs, end_of_input,
                    command, set_statement, grammar);


// The visitor class that does the final processing of the result
struct ResultVisitor : public boost::static_visitor<>
{
    ResultVisitor(pinloki::parser::Handler* handler)
        : m_handler(handler)
    {
    }

    void operator()(Select& s)
    {
        std::vector<std::string> names;
        std::vector<std::string> aliases;

        for (const auto& a : s.values)
        {
            names.push_back(get<std::string>(a.orig_name));
            aliases.push_back(get<std::string>(a.alias_name));

            if (aliases.back().empty())
            {
                aliases.back() = names.back();
            }
        }

        m_handler->select(names, aliases);
    }

    void operator()(Variable& a)
    {
        m_handler->set(a.key, get<std::string>(a.value));
    }

    void operator()(std::vector<Variable>& s)
    {
        for (const auto& a : s)
        {
            m_handler->set(a.key, get<std::string>(a.value));
        }
    }

    void operator()(Set& s)
    {
        boost::apply_visitor(*this, s.values);
    }

    void operator()(ChangeMaster& s)
    {
        if (!s.connection_name.empty())
        {
            MXS_SWARNING("Connection name ignored in CHANGE MASTER. "
                         "Multi-Source Replication is not supported by Binlog Router");
        }

        pinloki::parser::ChangeMasterValues changes;

        for (const auto& a : s.values)
        {
            changes.emplace(a.key, get<std::string>(a.value));
        }

        m_handler->change_master_to(changes);
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

    void operator()(PurgeLogs& s)
    {
        m_handler->purge_logs(s.up_to);
    }

    void operator()(ShowType& s)
    {
        switch (s)
        {
        case ShowType::MASTER_STATUS:
            m_handler->show_master_status();
            break;

        case ShowType::SLAVE_STATUS:
            m_handler->show_slave_status(false);
            break;

        case ShowType::ALL_SLAVES_STATUS:
            m_handler->show_slave_status(true);
            break;

        case ShowType::BINLOGS:
            m_handler->show_binlogs();
            break;
        }
    }

    void operator()(ShowVariables& s)
    {
        m_handler->show_variables(s.like);
    }

    void operator()(Show& s)
    {
        // For some reason boost doesn't expand the `show` rule into the correct type which is why a
        // x3::variant is used. This in turn requires the result to be processed twice with the same visitor.
        boost::apply_visitor(*this, s);
    }

    void operator()(MasterGtidWait& s)
    {
        m_handler->master_gtid_wait(s.gtid, s.timeout);
    }

    void operator()(nullptr_t&)
    {
        assert(!true);
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

    pinloki::parser::Handler* m_handler;
};
}

// Boost magic that automatically maps parse results to member variables. Needs to be done outside of the
// anonymous namespace (for some reason).
BOOST_FUSION_ADAPT_STRUCT(Variable, key, value);
BOOST_FUSION_ADAPT_STRUCT(ChangeMasterVariable, key, value);
BOOST_FUSION_ADAPT_STRUCT(Select, values);
BOOST_FUSION_ADAPT_STRUCT(Set, values);
BOOST_FUSION_ADAPT_STRUCT(ChangeMaster, connection_name, values);
BOOST_FUSION_ADAPT_STRUCT(ShowVariables, like);
BOOST_FUSION_ADAPT_STRUCT(PurgeLogs, up_to);
BOOST_FUSION_ADAPT_STRUCT(MasterGtidWait, gtid, timeout);
BOOST_FUSION_ADAPT_STRUCT(SelectField, orig_name, alias_name);

namespace pinloki
{
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
}
