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

namespace mxsmongo
{

namespace command
{

// https://docs.mongodb.com/manual/reference/command/availableQueryOptions/

// https://docs.mongodb.com/manual/reference/command/buildInfo/
class BuildInfo : public mxsmongo::Command
{
public:
    using mxsmongo::Command::Command;

    GWBUF* execute() override
    {
        using document_builder = bsoncxx::builder::basic::document;
        using array_builder = bsoncxx::builder::basic::array;
        using bsoncxx::builder::basic::kvp;

        document_builder buildInfo;

        array_builder versionArray;
        versionArray.append(MXSMONGO_VERSION_MAJOR);
        versionArray.append(MXSMONGO_VERSION_MINOR);
        versionArray.append(MXSMONGO_VERSION_PATCH);
        versionArray.append(0);

        array_builder storageEngines;
        document_builder openssl;
        openssl.append(kvp("running", OPENSSL_VERSION_TEXT));
        openssl.append(kvp("compiled", OPENSSL_VERSION_TEXT));
        array_builder modules;

#if defined(SS_DEBUG)
        bool debug = true;
#else
        bool debug = false;
#endif
        // Order the same as that in the documentation.
        buildInfo.append(kvp("gitVersion", MAXSCALE_COMMIT));
        buildInfo.append(kvp("versionArray", versionArray.extract()));
        buildInfo.append(kvp("version", MXSMONGO_VERSION));
        buildInfo.append(kvp("storageEngines", storageEngines.extract()));
        buildInfo.append(kvp("javascriptEngine", "mozjs")); // We lie
        buildInfo.append(kvp("bits", 64));
        buildInfo.append(kvp("debug", debug));
        buildInfo.append(kvp("maxBsonObjectSize", 16 * 1024 * 1024));
        buildInfo.append(kvp("opensll", openssl.extract()));
        buildInfo.append(kvp("modules", modules.extract()));
        buildInfo.append(kvp("ok", 1));

        buildInfo.append(kvp("maxscale", MAXSCALE_VERSION));

        return create_response(buildInfo.extract());
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
class GetCmdLineOpts : public mxsmongo::Command
{
public:
    using mxsmongo::Command::Command;

    GWBUF* execute() override
    {
        bsoncxx::builder::basic::document builder;

        bsoncxx::builder::basic::array argv_builder;
        argv_builder.append("maxscale");

        bsoncxx::builder::basic::array parsed_builder;

        builder.append(bsoncxx::builder::basic::kvp("argv", argv_builder.extract()));
        builder.append(bsoncxx::builder::basic::kvp("parsed", parsed_builder.extract()));
        builder.append(bsoncxx::builder::basic::kvp("ok", 1));

        return create_response(builder.extract());
    }
};


// https://docs.mongodb.com/manual/reference/command/getLog/
class GetLog : public mxsmongo::Command
{
public:
    using mxsmongo::Command::Command;

    GWBUF* execute() override
    {
        bsoncxx::builder::basic::document builder;

        auto element = m_doc[mxsmongo::key::GETLOG];

        const auto& utf8 = element.get_utf8().value;
        string value(utf8.data(), utf8.size());

        if (value == "*")
        {
            bsoncxx::builder::basic::array names_builder;
            names_builder.append("global");
            names_builder.append("startupWarnings");

            builder.append(bsoncxx::builder::basic::kvp("names", names_builder.extract()));
            builder.append(bsoncxx::builder::basic::kvp("ok", 1));
        }
        else if (value == "global" || value == "startupWarnings")
        {
            bsoncxx::builder::basic::array log_builder;

            builder.append(bsoncxx::builder::basic::kvp("totalLinesWritten", 0));
            builder.append(bsoncxx::builder::basic::kvp("log", log_builder.extract()));
            builder.append(bsoncxx::builder::basic::kvp("ok", 1));
        }
        else
        {
            string message("No RamLog names: ");
            message += value;

            builder.append(bsoncxx::builder::basic::kvp("ok", 0));
            builder.append(bsoncxx::builder::basic::kvp("errmsg", value));
        }

        return create_response(builder.extract());
    }
};


// https://docs.mongodb.com/manual/reference/command/hostInfo/

// https://docs.mongodb.com/manual/reference/command/isSelf/

// https://docs.mongodb.com/manual/reference/command/listCommands/

// https://docs.mongodb.com/manual/reference/command/lockInfo/

// https://docs.mongodb.com/manual/reference/command/netstat/

// https://docs.mongodb.com/manual/reference/command/ping/

// https://docs.mongodb.com/manual/reference/command/profile/

// https://docs.mongodb.com/manual/reference/command/serverStatus/

// https://docs.mongodb.com/manual/reference/command/shardConnPoolStats/

// https://docs.mongodb.com/manual/reference/command/top/

// https://docs.mongodb.com/manual/reference/command/validate/

// https://docs.mongodb.com/manual/reference/command/whatsmyuri/
class WhatsMyUri : public mxsmongo::Command
{
public:
    using mxsmongo::Command::Command;

    GWBUF* execute() override
    {
        bsoncxx::builder::basic::document builder;

        builder.append(bsoncxx::builder::basic::kvp("you", "127.0.0.1:49388"));
        builder.append(bsoncxx::builder::basic::kvp("ok", 1));

        return create_response(builder.extract());
    }
};


}

}
