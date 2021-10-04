/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
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
#include <maxscale/config.hh>
#include <maxscale/maxscale.h>

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

            doc.append(kvp(key::TOTAL_LINES_WRITTEN, 0));
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
