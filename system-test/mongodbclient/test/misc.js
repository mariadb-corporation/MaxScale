/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-01-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

const test = require('./mongotest');

const config = test.config;

const mariadb = test.mariadb;
const mongodb = test.mongodb;
const assert = test.assert;


describe('MISCALLENOUS', function () {
    let conn;
    let client;
    let collection;

    const valid_ids = [ "blah", 42, mongodb.ObjectID() ];

    before(async function () {
        // MariaDB
        conn = await test.MariaDB.createConnection();

        // MxsMongo
        client = await test.MxsMongo.createClient();

        // Ensure table will be autocreated.
        await conn.query("DROP TABLE IF EXISTS test.mongo");

        const database = client.db("test");
        collection = database.collection("mongo");
    });

    it('Can insert using client specified id', async function () {
        var o = { hello: "world"};

        for (var i = 0; i < valid_ids.length; ++i)
        {
            var id = valid_ids[i];

            o._id = id;
            var result = await collection.insertOne(o);
            assert.strictEqual(result.insertedCount, 1, "Should be able to insert a document.");
        }
    });

    it('Can fetch using client specified id', async function () {
        var query = {};

        for (var i = 0; i < valid_ids.length; ++i)
        {
            var id = valid_ids[i];

            query["_id"] = id;
            var doc = await collection.findOne(query);
            assert(doc)
            assert.deepStrictEqual(doc["_id"], id);
        }
    });

    it('Cannot insert many using same id', async function () {
        var o = { hello: "world", _id: "4711" };

        var result;

        result = await collection.insertOne(o);
        assert.strictEqual(result.insertedCount, 1, "Should be able to insert a document.");

        try {
            result = await collection.insertOne(o);
            assert(false, "Exception should be thrown.");
        }
        catch (x) {
            assert(x.writeErrors);
            assert(x.writeErrors.length == 1);
            assert(x.writeErrors[0].errmsg);
            assert(x.writeErrors[0].errmsg.indexOf("Duplicate entry") == 0);
        }
    });

    after(function () {
        if (client) {
            client.close();
        }

        if (conn) {
            conn.end();
        }
    });
});
