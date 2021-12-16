/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

//
// https://docs.mongodb.com/v4.4/reference/command/nav-replication/
//

#include "defs.hh"

namespace nosql
{

namespace command
{

// https://docs.mongodb.com/v4.4/reference/command/applyOps/

// https://docs.mongodb.com/v4.4/reference/command/isMaster/
class IsMaster final : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "isMaster";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

    void populate_response(DocumentBuilder& doc) override
    {
        populate_response(m_database, m_doc, doc);
    }

    static void populate_response(Database& database,
                                  const bsoncxx::document::view& query,
                                  DocumentBuilder& doc)
    {
        auto client = query[key::CLIENT];

        bool metadata_sent = database.context().metadata_sent();

        if (client && metadata_sent)
        {
            throw SoftError("The client metadata document may only be sent in the first isMaster",
                            error::CLIENT_METADATA_CANNOT_BE_MUTATED);
        }
        else if (!client && !metadata_sent)
        {
            throw SoftError("The client metadata document must be sent in the first isMaster",
                            error::CLIENT_METADATA_MISSING_FIELD);
        }
        else if (client && !metadata_sent)
        {
            database.context().set_metadata_sent(true);
        }

        doc.append(kvp(key::ISMASTER, true));
        doc.append(kvp(key::TOPOLOGY_VERSION, topology_version()));
        doc.append(kvp(key::MAX_BSON_OBJECT_SIZE, protocol::MAX_BSON_OBJECT_SIZE));
        doc.append(kvp(key::MAX_MESSAGE_SIZE_BYTES, protocol::MAX_MSG_SIZE));
        doc.append(kvp(key::MAX_WRITE_BATCH_SIZE, protocol::MAX_WRITE_BATCH_SIZE));
        doc.append(kvp(key::LOCAL_TIME, bsoncxx::types::b_date(std::chrono::system_clock::now())));
        doc.append(kvp(key::LOGICAL_SESSION_TIMEOUT_MINUTES, 30));
        doc.append(kvp(key::CONNECTION_ID, database.context().connection_id()));
        doc.append(kvp(key::MIN_WIRE_VERSION, MIN_WIRE_VERSION));
        doc.append(kvp(key::MAX_WIRE_VERSION, MAX_WIRE_VERSION));
        doc.append(kvp(key::READ_ONLY, false));

        // TODO: Handle "speculativeAuthenticate";

        auto element = query[key::SASL_SUPPORTED_MECHS];

        if (element)
        {
            if (element.type() != bsoncxx::type::k_utf8)
            {
                ostringstream ss;
                ss << "\"" << key::SASL_SUPPORTED_MECHS << "\" had the wrong type. Expected string, "
                   << "found " << bsoncxx::to_string(element.type()) << ".";

                throw SoftError(ss.str(), error::TYPE_MISMATCH);
            }

            auto user = static_cast<string_view>(element.get_utf8());

            auto& context = database.context();

            if (context.um().user_exists(user))
            {
                ArrayBuilder sasl_supported_mechs;

                sasl_supported_mechs.append("SCRAM-SHA-1");

                doc.append(kvp(key::SASL_SUPPORTED_MECHS, sasl_supported_mechs.extract()));
            }
        }

        doc.append(kvp(key::OK, 1));
    }
};


// https://docs.mongodb.com/v4.4/reference/command/replSetAbortPrimaryCatchUp/

// https://docs.mongodb.com/v4.4/reference/command/replSetFreeze/

// https://docs.mongodb.com/v4.4/reference/command/replSetGetConfig/

// https://docs.mongodb.com/v4.4/reference/command/replSetGetStatus/
class ReplSetGetStatus final : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "replSetGetStatus";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

    void populate_response(DocumentBuilder& doc) override
    {
        throw SoftError("not running with --replSet", error::NO_REPLICATION_ENABLED);
    }
};


// https://docs.mongodb.com/v4.4/reference/command/replSetInitiate/

// https://docs.mongodb.com/v4.4/reference/command/replSetMaintenance/

// https://docs.mongodb.com/v4.4/reference/command/replSetReconfig/

// https://docs.mongodb.com/v4.4/reference/command/replSetResizeOplog/

// https://docs.mongodb.com/v4.4/reference/command/replSetStepDown/

// https://docs.mongodb.com/v4.4/reference/command/replSetSyncFrom/


}

}
