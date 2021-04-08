/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

//
// https://docs.mongodb.com/manual/reference/command/nav-diagnostic/
//

#include "defs.hh"
#include <openssl/opensslv.h>
#include <maxscale/config.hh>

namespace mxsmongo
{

namespace command
{

// https://docs.mongodb.com/manual/reference/command/availableQueryOptions/

// https://docs.mongodb.com/manual/reference/command/buildInfo/
class BuildInfo final : public ImmediateCommand
{
public:
    using ImmediateCommand::ImmediateCommand;

    void populate_response(DocumentBuilder& doc) override
    {
        ArrayBuilder versionArray;
        versionArray.append(MONGO_VERSION_MAJOR);
        versionArray.append(MONGO_VERSION_MINOR);
        versionArray.append(MONGO_VERSION_PATCH);
        versionArray.append(0);

        ArrayBuilder storageEngines;
        DocumentBuilder openssl;
        openssl.append(kvp("running", OPENSSL_VERSION_TEXT));
        openssl.append(kvp("compiled", OPENSSL_VERSION_TEXT));
        ArrayBuilder modules;

#if defined(SS_DEBUG)
        bool debug = true;
#else
        bool debug = false;
#endif
        // Order the same as that in the documentation.
        doc.append(kvp("gitVersion", MAXSCALE_COMMIT));
        doc.append(kvp("versionArray", versionArray.extract()));
        doc.append(kvp("version", MONGO_ZVERSION));
        doc.append(kvp("storageEngines", storageEngines.extract()));
        doc.append(kvp("javascriptEngine", "mozjs")); // We lie
        doc.append(kvp("bits", 64));
        doc.append(kvp("debug", debug));
        doc.append(kvp("maxBsonObjectSize", mongo::MAX_BSON_OBJECT_SIZE));
        doc.append(kvp("opensll", openssl.extract()));
        doc.append(kvp("modules", modules.extract()));
        doc.append(kvp("ok", 1));

        doc.append(kvp("maxscale", MAXSCALE_VERSION));
    }
};


// https://docs.mongodb.com/manual/reference/command/collStats/

// https://docs.mongodb.com/manual/reference/command/connPoolStats/

// https://docs.mongodb.com/manual/reference/command/connectionStatus/

// https://docs.mongodb.com/manual/reference/command/cursorInfo/

// https://docs.mongodb.com/manual/reference/command/dataSize/

// https://docs.mongodb.com/manual/reference/command/dbHash/

// https://docs.mongodb.com/manual/reference/command/dbStats/

// https://docs.mongodb.com/manual/reference/command/diagLogging/

// https://docs.mongodb.com/manual/reference/command/driverOIDTest/

// https://docs.mongodb.com/manual/reference/command/explain/

// https://docs.mongodb.com/manual/reference/command/features/

// https://docs.mongodb.com/manual/reference/command/getCmdLineOpts/
class GetCmdLineOpts final : public ImmediateCommand
{
public:
    using ImmediateCommand::ImmediateCommand;

    void populate_response(DocumentBuilder& doc) override
    {
        auto& config = mxs::Config::get();

        ArrayBuilder argv;
        for (const auto arg : config.argv)
        {
            argv.append(arg);
        }

        ArrayBuilder parsed;

        doc.append(kvp("argv", argv.extract()));
        doc.append(kvp("parsed", parsed.extract()));
        doc.append(kvp("ok", 1));
    }
};


// https://docs.mongodb.com/manual/reference/command/getLog/
class GetLog final : public ImmediateCommand
{
public:
    using ImmediateCommand::ImmediateCommand;

    void populate_response(DocumentBuilder& doc) override
    {
        auto value = value_as<string>();

        if (value == "*")
        {
            ArrayBuilder names;
            names.append("global");
            names.append("startupWarnings");

            doc.append(kvp("names", names.extract()));
            doc.append(kvp("ok", 1));
        }
        else if (value == "global" || value == "startupWarnings")
        {
            ArrayBuilder log;

            doc.append(kvp("totalLinesWritten", 0));
            doc.append(kvp("log", log.extract()));
            doc.append(kvp("ok", 1));
        }
        else
        {
            string message("No RamLog names: ");
            message += value;

            doc.append(kvp("ok", 0));
            doc.append(kvp("errmsg", value));
        }
    }
};


// https://docs.mongodb.com/manual/reference/command/hostInfo/

// https://docs.mongodb.com/manual/reference/command/isSelf/

// https://docs.mongodb.com/manual/reference/command/listCommands/

// https://docs.mongodb.com/manual/reference/command/lockInfo/

// https://docs.mongodb.com/manual/reference/command/netstat/

// https://docs.mongodb.com/manual/reference/command/ping/
class Ping final : public ImmediateCommand
{
public:
    using ImmediateCommand::ImmediateCommand;

    void populate_response(DocumentBuilder& doc) override
    {
        doc.append(kvp("ok", 1));
    }
};

// https://docs.mongodb.com/manual/reference/command/profile/

// https://docs.mongodb.com/manual/reference/command/serverStatus/

// https://docs.mongodb.com/manual/reference/command/shardConnPoolStats/

// https://docs.mongodb.com/manual/reference/command/top/

// https://docs.mongodb.com/manual/reference/command/validate/

// https://docs.mongodb.com/manual/reference/command/whatsmyuri/
class WhatsMyUri final : public ImmediateCommand
{
public:
    using ImmediateCommand::ImmediateCommand;

    void populate_response(DocumentBuilder& doc) override
    {
        ClientDCB* pDcb = m_database.context().client_connection().dcb();

        stringstream you;
        you << pDcb->client_remote() << ":" << pDcb->port();

        doc.append(kvp("you", you.str()));
        doc.append(kvp("ok", 1));
    }
};


}

}
