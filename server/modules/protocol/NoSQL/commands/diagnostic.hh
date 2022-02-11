/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

//
// https://docs.mongodb.com/v4.4/reference/command/nav-diagnostic/
//

#include "defs.hh"
#include <openssl/opensslv.h>
#include <map>
#include <maxscale/config.hh>
#include <maxscale/maxscale.h>
#include "query_and_write_operation.hh"

namespace nosql
{

namespace command
{

// https://docs.mongodb.com/v4.4/reference/command/availableQueryOptions/

// https://docs.mongodb.com/v4.4/reference/command/buildInfo/
class BuildInfo final : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "buildInfo";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

    void populate_response(DocumentBuilder& doc) override
    {
        ArrayBuilder versionArray;
        versionArray.append(NOSQL_VERSION_MAJOR);
        versionArray.append(NOSQL_VERSION_MINOR);
        versionArray.append(NOSQL_VERSION_PATCH);
        versionArray.append(0);

        ArrayBuilder storageEngines;
        DocumentBuilder openssl;
        openssl.append(kvp(key::RUNNING, OPENSSL_VERSION_TEXT));
        openssl.append(kvp(key::COMPILED, OPENSSL_VERSION_TEXT));
        ArrayBuilder modules;

#if defined(SS_DEBUG)
        bool debug = true;
#else
        bool debug = false;
#endif
        // Order the same as that in the documentation.
        doc.append(kvp(key::GIT_VERSION, MAXSCALE_COMMIT));
        doc.append(kvp(key::VERSION_ARRAY, versionArray.extract()));
        doc.append(kvp(key::VERSION, NOSQL_ZVERSION));
        doc.append(kvp(key::STORAGE_ENGINES, storageEngines.extract()));
        doc.append(kvp(key::JAVASCRIPT_ENGINE, value::MOZJS)); // We lie
        doc.append(kvp(key::BITS, 64));
        doc.append(kvp(key::DEBUG, debug));
        doc.append(kvp(key::MAX_BSON_OBJECT_SIZE, protocol::MAX_BSON_OBJECT_SIZE));
        doc.append(kvp(key::OPENSSL, openssl.extract()));
        doc.append(kvp(key::MODULES, modules.extract()));
        doc.append(kvp(key::OK, 1));

        doc.append(kvp(key::MAXSCALE, MAXSCALE_VERSION));
    }
};


// https://docs.mongodb.com/v4.4/reference/command/collStats/

// https://docs.mongodb.com/v4.4/reference/command/connPoolStats/

// https://docs.mongodb.com/v4.4/reference/command/connectionStatus/

// https://docs.mongodb.com/v4.4/reference/command/cursorInfo/

// https://docs.mongodb.com/v4.4/reference/command/dataSize/

// https://docs.mongodb.com/v4.4/reference/command/dbHash/

// https://docs.mongodb.com/v4.4/reference/command/dbStats/

// https://docs.mongodb.com/v4.4/reference/command/diagLogging/

// https://docs.mongodb.com/v4.4/reference/command/driverOIDTest/

// https://docs.mongodb.com/v4.4/reference/command/explain/
class Explain final : public OpMsgCommand
{
public:
    static constexpr const char* const KEY = "explain";
    static constexpr const char* const HELP = "";

    using OpMsgCommand::OpMsgCommand;

    enum class Verbosity
    {
        QUERY_PLANNER,
        EXECUTION_STATS,
        ALL_PLANS_EXECUTION,
    };

    State execute(GWBUF** ppNoSQL_response) override final
    {
        string s;
        if (optional(key::VERBOSITY, &s))
        {
            if (s == "queryPlanner")
            {
                m_verbosity = Verbosity::QUERY_PLANNER;
            }
            else if (s == "executionStats")
            {
                m_verbosity = Verbosity::EXECUTION_STATS;
            }
            else if (s == "allPlansExecution")
            {
                m_verbosity = Verbosity::ALL_PLANS_EXECUTION;
            }
            else
            {
                throw SoftError("verbosity string must be one of {'queryPlanner', "
                                "'executionStats', 'allPlansExecution'}",
                                error::FAILED_TO_PARSE);
            }
        }

        auto explain = value_as<bsoncxx::document::view>();

        auto it = explain.begin();

        if (it != explain.end())
        {
            string_view sv = explain[it->key()].get_utf8();
            string collection = static_cast<string>(sv);

            auto create = command_creator_for(mxb::tolower(static_cast<string>(it->key())));

            m_sSub_command.reset(create(this, collection, explain));
        }
        else
        {
            throw SoftError("Explain failed due to unknown command: ", error::COMMAND_NOT_FOUND);
        }

        return m_sSub_command->execute(ppNoSQL_response);
    }

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response) override final
    {
        mxb_assert(m_sSub_command.get());

        return m_sSub_command->translate(std::move(mariadb_response), ppNoSQL_response);
    }

    void diagnose(DocumentBuilder& doc) override final
    {
        // TODO: Add more.
        doc.append(kvp(key::KIND, value::MULTI));
        doc.append(kvp(key::OK, 1));
    }

private:
    class SubCommand
    {
    public:
        SubCommand(Explain* pSuper,
                   const string& collection,
                   const bsoncxx::document::view& doc)
            : m_super(*pSuper)
            , m_doc(doc)
        {
            initialize_query_planner(collection);
        }

        virtual ~SubCommand()
        {
        }

        virtual State execute(GWBUF** ppNoSQL_response) = 0;
        virtual State translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response) = 0;

    protected:
        void add_execution_stats(DocumentBuilder& doc)
        {
            DocumentBuilder es;

            if (m_super.m_verbosity == Verbosity::ALL_PLANS_EXECUTION)
            {
                ArrayBuilder ape;

                doc.append(kvp(key::ALL_PLANS_EXECUTION, ape.extract()));
            }

            doc.append(kvp(key::EXECUTION_STATS, es.extract()));
        }

        void add_server_info(DocumentBuilder& doc, int ok)
        {
            const auto& config = mxs::Config::get();

            DocumentBuilder server_info;
            server_info.append(kvp(key::HOST, config.nodename));
            server_info.append(kvp(key::PORT, 17017)); // TODO: Make the port available.
            server_info.append(kvp(key::VERSION, NOSQL_ZVERSION));
            server_info.append(kvp(key::GIT_VERSION, MAXSCALE_COMMIT));

            doc.append(kvp(key::SERVER_INFO, server_info.extract()));

            doc.append(kvp(key::OK, ok));
        }

        Explain&                 m_super;
        bsoncxx::document::view  m_doc;
        DocumentBuilder          m_query_planner;
        DocumentArguments        m_arguments;
        unique_ptr<OpMsgCommand> m_sCommand;

    private:
        void initialize_query_planner(const string& collection_name)
        {
            string ns = m_super.m_database.name() + "." + collection_name;
            ArrayBuilder rejected_plans;

            m_query_planner.append(kvp(key::PLANNER_VERSION, 1));
            m_query_planner.append(kvp(key::NS, ns));
            m_query_planner.append(kvp(key::INDEX_FILTER_SET, false));
            m_query_planner.append(kvp(key::INDEX_FILTER_SET, false));
            m_query_planner.append(kvp(key::REJECTED_PLANS, rejected_plans.extract()));
        }
    };

    class DefaultSubCommand : public SubCommand
    {
    public:
        using SubCommand::SubCommand;

        static SubCommand* create(Explain* pSuper,
                                  const string& collection,
                                  const bsoncxx::document::view& doc)
        {
            return new DefaultSubCommand(pSuper, collection, doc);
        }

        State execute(GWBUF** ppResponse) override final
        {
            DocumentBuilder doc;

            doc.append(kvp(key::QUERY_PLANNER, m_query_planner.extract()));

            if (m_super.m_verbosity != Verbosity::QUERY_PLANNER)
            {
                add_execution_stats(doc);
            }

            add_server_info(doc, 1);

            *ppResponse = m_super.create_response(doc.extract());
            return State::READY;
        }

        State translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response) override final
        {
            mxb_assert(!true);
            return State::READY;
        }
    };

    class FindSubCommand : public SubCommand
    {
    public:
        using SubCommand::SubCommand;

        static SubCommand* create(Explain* pSuper,
                                  const string& collection,
                                  const bsoncxx::document::view& doc)
        {
            return new FindSubCommand(pSuper, collection, doc);
        }

        State execute(GWBUF** ppResponse) override final
        {
            auto filter = m_doc[key::FILTER];

            if (filter)
            {
                append(m_query_planner, key::PARSED_QUERY, filter);
            }

            DocumentBuilder winning_plan;
            winning_plan.append(kvp(key::STAGE, "COLLSCAN"));
            if (filter)
            {
                append(winning_plan, key::FILTER, filter);
            }
            winning_plan.append(kvp(key::DIRECTION, "forward"));

            m_query_planner.append(kvp(key::WINNING_PLAN, winning_plan.extract()));

            packet::Msg req(m_super.m_req);
            m_sCommand.reset(new Find(Find::KEY,
                                      &m_super.m_database,
                                      m_super.m_pRequest,
                                      std::move(req),
                                      m_doc,
                                      m_arguments,
                                      &m_find_stats));

            return m_sCommand->execute(ppResponse);
        }

        State translate(mxs::Buffer&& response, GWBUF** ppResponse) override final
        {
            mxb_assert(m_sCommand.get());

            GWBUF* pResponse = nullptr;
            m_sCommand->translate(std::move(response), &pResponse);
            gwbuf_free(pResponse);

            DocumentBuilder doc;
            doc.append(kvp(key::QUERY_PLANNER, m_query_planner.extract()));

            if (m_super.m_verbosity != Verbosity::QUERY_PLANNER)
            {
                DocumentBuilder execution_stats;
                execution_stats.append(kvp(key::EXECUTION_SUCCESS, true));
                execution_stats.append(kvp(key::N_RETURNED, m_find_stats.nReturned));

                doc.append(kvp(key::EXECUTION_STATS, execution_stats.extract()));
            }

            add_server_info(doc, 1);

            *ppResponse = m_super.create_response(doc.extract());

            return State::READY;
        }

    private:
        Find::Stats m_find_stats;
    };

    using create_function = SubCommand* (*)(Explain*, const string&, const bsoncxx::document::view&);

    create_function command_creator_for(const string& command)
    {
        auto it = s_commands.find(command);

        if (it == s_commands.end())
        {
            ostringstream ss;
            ss << "Explain failed due to unknown command: " << command;
            throw SoftError(ss.str(), error::COMMAND_NOT_FOUND);
        }

        return it->second;
    }

    static map<string, create_function> s_commands;

    Verbosity              m_verbosity = Verbosity::QUERY_PLANNER;
    unique_ptr<SubCommand> m_sSub_command;
};

//static
map<string, Explain::create_function> Explain::s_commands =
{
    // NOTE: All lower case.
    { "aggregate",     &Explain::DefaultSubCommand::create },
    { "count",         &Explain::DefaultSubCommand::create },
    { "delete",        &Explain::DefaultSubCommand::create },
    { "distinct",      &Explain::DefaultSubCommand::create },
    { "find",          &Explain::FindSubCommand::create },
    { "findandmodify", &Explain::DefaultSubCommand::create },
    { "mapreduce",     &Explain::DefaultSubCommand::create },
    { "update",        &Explain::DefaultSubCommand::create },
};


// https://docs.mongodb.com/v4.4/reference/command/features/

// https://docs.mongodb.com/v4.4/reference/command/getCmdLineOpts/
class GetCmdLineOpts final : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "getCmdLineOpts";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

    void populate_response(DocumentBuilder& doc) override
    {
        auto& config = mxs::Config::get();

        ArrayBuilder argv;
        for (const auto& arg : config.argv)
        {
            argv.append(arg);
        }

        ArrayBuilder parsed;

        doc.append(kvp(key::ARGV, argv.extract()));
        doc.append(kvp(key::PARSED, parsed.extract()));
        doc.append(kvp(key::OK, 1));
    }
};

// https://docs.mongodb.com/v4.4/reference/command/getLog/
class GetLog;

template<>
struct IsAdmin<command::GetLog>
{
    static const bool is_admin { true };
};

class GetLog final : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "getLog";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

    bool is_admin() const override
    {
        return IsAdmin<GetLog>::is_admin;
    }

    void populate_response(DocumentBuilder& doc) override
    {
        auto value = value_as<string>();

        if (value == "*")
        {
            ArrayBuilder names;
            names.append("global");
            names.append("startupWarnings");

            doc.append(kvp(key::NAMES, names.extract()));
            doc.append(kvp(key::OK, 1));
        }
        else if (value == "global" || value == "startupWarnings")
        {
            ArrayBuilder log;
            log.append("No news is good news."); // TODO: The MaxScale log could be returned.

            doc.append(kvp(key::TOTAL_LINES_WRITTEN, 1));
            doc.append(kvp(key::LOG, log.extract()));
            doc.append(kvp(key::OK, 1));
        }
        else
        {
            string message("No RamLog named: ");
            message += value;

            doc.append(kvp(key::OK, 0));
            doc.append(kvp(key::ERRMSG, value));
        }
    }
};


// https://docs.mongodb.com/v4.4/reference/command/hostInfo/
class HostInfo;

template<>
struct IsAdmin<command::HostInfo>
{
    static const bool is_admin { true };
};

class HostInfo final : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "hostInfo";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

    bool is_admin() const override
    {
        return IsAdmin<HostInfo>::is_admin;
    }

    void populate_response(DocumentBuilder& doc) override
    {
        long memory = get_total_memory();

        const auto& config = mxs::Config::get();

        DocumentBuilder system;
        system.append(kvp(key::CURRENT_TIME, bsoncxx::types::b_date(std::chrono::system_clock::now())));
        system.append(kvp(key::HOSTNAME, config.nodename));
        system.append(kvp(key::CPU_ADDR_SIZE, 64));
        system.append(kvp(key::MEM_SIZE_MB, memory));
        system.append(kvp(key::MEM_LIMIT_MB, memory));
        system.append(kvp(key::NUM_CORES, (int)get_processor_count()));
        system.append(kvp(key::CPU_ARCH, config.machine));
        system.append(kvp(key::NUMA_ENABLED, false));

        DocumentBuilder os;
        os.append(kvp(key::TYPE, config.sysname));
        // TODO: Enhance config.c:get_release_string() so that you can get the information
        // TODO: in a structured format.
        os.append(kvp(key::NAME, "Unknown"));
        os.append(kvp(key::VERSION, "Unknown"));

        DocumentBuilder extra;

        doc.append(kvp(key::SYSTEM, system.extract()));
        doc.append(kvp(key::OS, os.extract()));
        doc.append(kvp(key::EXTRA, extra.extract()));

        doc.append(kvp(key::OK, 1));
    }
};

// https://docs.mongodb.com/v4.4/reference/command/isSelf/

// https://docs.mongodb.com/v4.4/reference/command/listCommands/
class ListCommands final : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "listCommands";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

    void populate_response(DocumentBuilder& doc) override
    {
        DocumentBuilder commands;

        OpMsgCommand::list_commands(commands);

        doc.append(kvp(key::COMMANDS, commands.extract()));
        doc.append(kvp(key::OK, 1));
    }
};

// https://docs.mongodb.com/v4.4/reference/command/lockInfo/

// https://docs.mongodb.com/v4.4/reference/command/netstat/

// https://docs.mongodb.com/v4.4/reference/command/ping/
class Ping final : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "ping";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

    void populate_response(DocumentBuilder& doc) override
    {
        doc.append(kvp(key::OK, 1));
    }
};

// https://docs.mongodb.com/v4.4/reference/command/profile/

// https://docs.mongodb.com/v4.4/reference/command/serverStatus/
class ServerStatus final : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "serverStatus";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

    void populate_response(DocumentBuilder& doc) override
    {
        DocumentBuilder asserts;
        DocumentBuilder connections; // TODO: Populate this.
        DocumentBuilder election_metrics;
        DocumentBuilder extra_info;
        DocumentBuilder flow_control;
        DocumentBuilder storage_engine;
        storage_engine.append(kvp(key::NAME, key::MARIADB));
        int uptime_seconds = maxscale_uptime();

        doc.append(kvp(key::ASSERTS, asserts.extract()));
        doc.append(kvp(key::CONNECTIONS, connections.extract()));
        doc.append(kvp(key::ELECTION_METRICS, election_metrics.extract()));
        doc.append(kvp(key::EXTRA_INFO, election_metrics.extract()));
        doc.append(kvp(key::FLOW_CONTROL, flow_control.extract()));
        doc.append(kvp(key::LOCAL_TIME, bsoncxx::types::b_date(std::chrono::system_clock::now())));
        doc.append(kvp(key::PID, getpid()));
        doc.append(kvp(key::STORAGE_ENGINE, storage_engine.extract()));
        doc.append(kvp(key::UPTIME, uptime_seconds));
        doc.append(kvp(key::UPTIME_ESTIMATE, uptime_seconds));
        doc.append(kvp(key::UPTIME_MILLIS, uptime_seconds * 1000));
        doc.append(kvp(key::OK, 1));
    }
};

// https://docs.mongodb.com/v4.4/reference/command/shardConnPoolStats/

// https://docs.mongodb.com/v4.4/reference/command/top/

// https://docs.mongodb.com/v4.4/reference/command/validate/
class Validate final : public SingleCommand
{
public:
    static constexpr const char* const KEY = "validate";
    static constexpr const char* const HELP = "";

    using SingleCommand::SingleCommand;

    string generate_sql() override
    {
        ostringstream ss;
        ss << "SELECT COUNT(id) FROM " << table();
        return ss.str();
    }

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppResponse) override
    {
        ComResponse response(mariadb_response.data());

        int32_t ok = 0;
        int32_t n = 0;

        switch (response.type())
        {
        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                auto code = err.code();

                if (code == ER_NO_SUCH_TABLE)
                {
                    ostringstream ss;
                    ss << "Collection '" << table(Quoted::NO) << "' does not exist to validate.";

                    throw SoftError(ss.str(), error::NAMESPACE_NOT_FOUND);
                }
                else
                {
                    throw MariaDBError(err);
                }
            }
            break;

        case ComResponse::OK_PACKET:
        case ComResponse::LOCAL_INFILE_PACKET:
            mxb_assert(!true);
            throw_unexpected_packet();

        default:
            ok = 1;
            n = get_n(GWBUF_DATA(mariadb_response.get()));
        }

        DocumentBuilder doc;

        int32_t n_invalid_documents = 0;
        int32_t n_indexes = 1;

        DocumentBuilder keys_per_index;
        keys_per_index.append(kvp(key::_ID_, n));

        DocumentBuilder id;
        id.append(kvp(key::VALID, true));
        DocumentBuilder index_details;
        index_details.append(kvp(key::_ID_, id.extract()));

        ArrayBuilder empty_array;

        doc.append(kvp(key::NS, table(Quoted::NO)));
        doc.append(kvp(key::N_INVALID_DOCUMENTS, n_invalid_documents));
        doc.append(kvp(key::NRECORDS, n));
        doc.append(kvp(key::N_INDEXES, n_indexes));
        doc.append(kvp(key::KEYS_PER_INDEX, keys_per_index.extract()));
        doc.append(kvp(key::INDEX_DETAILS, index_details.extract()));
        doc.append(kvp(key::VALID, true));
        doc.append(kvp(key::WARNINGS, empty_array.extract()));
        doc.append(kvp(key::ERRORS,empty_array.extract()));
        doc.append(kvp(key::EXTRA_INDEX_ENTRIES, empty_array.extract()));
        doc.append(kvp(key::MISSING_INDEX_ENTRIES, empty_array.extract()));
        doc.append(kvp(key::OK, ok));

        *ppResponse = create_response(doc.extract());
        return State::READY;
    }

private:
    int32_t get_n(uint8_t* pBuffer)
    {
        int32_t n = 0;

        ComQueryResponse cqr(&pBuffer);
        mxb_assert(cqr.nFields());

        ComQueryResponse::ColumnDef column_def(&pBuffer);
        vector<enum_field_types> types { column_def.type() };

        ComResponse eof(&pBuffer);
        mxb_assert(eof.type() == ComResponse::EOF_PACKET);

        CQRTextResultsetRow row(&pBuffer, types);

        auto it = row.begin();
        mxb_assert(it != row.end());

        const auto& value = *it++;
        mxb_assert(it == row.end());

        n = std::stoi(value.as_string().to_string());

        return n;
    }
};

// https://docs.mongodb.com/v4.4/reference/command/whatsmyuri/
class WhatsMyUri final : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "whatsmyuri";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

    void populate_response(DocumentBuilder& doc) override
    {
        ClientDCB* pDcb = m_database.context().client_connection().dcb();

        ostringstream you;
        you << pDcb->client_remote() << ":" << pDcb->port();

        doc.append(kvp(key::YOU, you.str()));
        doc.append(kvp(key::OK, 1));
    }
};


}

}
