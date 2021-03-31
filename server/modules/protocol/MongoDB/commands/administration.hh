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
// https://docs.mongodb.com/manual/reference/command/nav-administration/
//

#include "defs.hh"

namespace mxsmongo
{

namespace command
{

// https://docs.mongodb.com/manual/reference/command/cloneCollectionAsCapped/

// https://docs.mongodb.com/manual/reference/command/collMod/

// https://docs.mongodb.com/manual/reference/command/compact/

// https://docs.mongodb.com/manual/reference/command/connPoolSync/

// https://docs.mongodb.com/manual/reference/command/convertToCapped/

// https://docs.mongodb.com/manual/reference/command/create/
class Create : public Command
{
public:
    using Command::Command;

    GWBUF* execute() override
    {
        stringstream sql;

        sql << "CREATE TABLE " << table() << " (id TEXT NOT NULL UNIQUE, doc JSON)";

        send_downstream(sql.str());

        return nullptr;
    }

    State translate(GWBUF& mariadb_response, GWBUF** ppResponse) override
    {
        ComResponse response(GWBUF_DATA(&mariadb_response));

        int32_t ok = 0;

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            ok = 1;
            break;

        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                if (err.code() == ER_TABLE_EXISTS_ERROR)
                {
                    stringstream ss;
                    ss << "Collection already exists. NS: " << table(Quoted::NO);
                    throw SoftError(ss.str(), error::NAMESPACE_EXISTS);
                }
                else
                {
                    throw MariaDBError(ComERR(response));
                }
            }
            break;

        case ComResponse::LOCAL_INFILE_PACKET:
            mxb_assert(!true);
            break;

        default:
            mxb_assert(!true);
        }

        DocumentBuilder doc;

        doc.append(kvp("ok", ok));

        *ppResponse = create_response(doc.extract());
        return READY;
    }
};


// https://docs.mongodb.com/manual/reference/command/createIndexes/

// https://docs.mongodb.com/manual/reference/command/currentOp/

// https://docs.mongodb.com/manual/reference/command/drop/
class Drop : public Command
{
public:
    using Command::Command;

    GWBUF* execute() override
    {
        stringstream sql;

        sql << "DROP TABLE " << table();

        send_downstream(sql.str());

        return nullptr;
    }

    State translate(GWBUF& mariadb_response, GWBUF** ppResponse) override
    {
        ComResponse response(GWBUF_DATA(&mariadb_response));

        int32_t ok = 0;

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            ok = 1;
            break;

        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                if (err.code() == ER_BAD_TABLE_ERROR)
                {
                    throw SoftError("ns not found", error::NAMESPACE_NOT_FOUND);
                }
                else
                {
                    throw MariaDBError(ComERR(response));
                }
            }
            break;

        case ComResponse::LOCAL_INFILE_PACKET:
            mxb_assert(!true);
            break;

        default:
            mxb_assert(!true);
        }

        DocumentBuilder doc;

        doc.append(kvp("ok", ok));
        doc.append(kvp("ns", table(Quoted::NO)));
        doc.append(kvp("nIndexesWas", 1)); // TODO: Report real value.

        *ppResponse = create_response(doc.extract());
        return READY;
    }
};


// https://docs.mongodb.com/manual/reference/command/dropDatabase/

// https://docs.mongodb.com/manual/reference/command/dropConnections/

// https://docs.mongodb.com/manual/reference/command/dropIndexes/

// https://docs.mongodb.com/manual/reference/command/filemd5/

// https://docs.mongodb.com/manual/reference/command/fsync/

// https://docs.mongodb.com/manual/reference/command/fsyncUnlock/

// https://docs.mongodb.com/manual/reference/command/getDefaultRWConcern/

// https://docs.mongodb.com/manual/reference/command/getParameter/

// https://docs.mongodb.com/manual/reference/command/killCursors/

// https://docs.mongodb.com/manual/reference/command/killOp/

// https://docs.mongodb.com/manual/reference/command/listCollections/

// https://docs.mongodb.com/manual/reference/command/listDatabases/

// https://docs.mongodb.com/manual/reference/command/listIndexes/

// https://docs.mongodb.com/manual/reference/command/logRotate/

// https://docs.mongodb.com/manual/reference/command/reIndex/

// https://docs.mongodb.com/manual/reference/command/renameCollection/

// https://docs.mongodb.com/manual/reference/command/setFeatureCompatibilityVersion/

// https://docs.mongodb.com/manual/reference/command/setIndexCommitQuorum/

// https://docs.mongodb.com/manual/reference/command/setParameter/

// https://docs.mongodb.com/manual/reference/command/setDefaultRWConcern/

// https://docs.mongodb.com/manual/reference/command/shutdown/


}

}
