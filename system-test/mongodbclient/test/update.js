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

// https://docs.mongodb.com/drivers/node/usage-examples/updateOne/

const mariadb = require('mariadb');
const { MongoClient } = require('mongodb');

const assert = require('assert');

var host = process.env.maxscale_000_network;
var mariadb_port = 4008;
var mongodb_port = 4711;
var user = 'maxskysql';
var password = 'skysql';

before(async function () {
    if (!process.env.maxscale_000_network) {
        throw new Error("The environment variable 'maxscale_000_network' must be set.");
    }
});

describe('UPDATE', function () {
    let conn;
    let client;
    let collection;
    const N = 20;

    before(async function () {
        // MariaDB
        conn = await mariadb.createConnection({
            host: host,
            port: mariadb_port,
            user: user,
            password: password });

        await conn.query("USE test");
        await conn.query("DROP TABLE IF EXISTS mongo");
        await conn.query("CREATE TABLE mongo "
                         + "(id VARCHAR(36) AS (TRIM('\"' FROM JSON_COMPACT(JSON_EXTRACT(doc, \"$._id\")))) "
                         + "UNIQUE KEY, "
                         + "doc JSON, "
                         + "CONSTRAINT id_not_null CHECK(id IS NOT NULL))");

        // MxsMongo
        var uri = "mongodb://" + host + ":" + mongodb_port;

        client = new MongoClient(uri, { useUnifiedTopology: true });
        await client.connect();

        const database = client.db("test");
        collection = database.collection("mongo");

        var documents = [];

        for (var i = 0; i < N; ++i)
        {
            var doc = { field: i };
            documents.push(doc);
        }

        const result = await collection.insertMany(documents);

        assert.strictEqual(result.insertedCount, documents.length, "Should be able to insert documents.");
    });

    it('$set', async function () {

        const filter = {
            field: Math.round(N/2)
        };

        const update = {
            "$set": {
                field: N + 1
            }
        };

        var result;

        result = await collection.updateOne(filter, update);
        assert.strictEqual(result.matchedCount, 1);

        var doc = await collection.findOne({field: N + 1});
        assert(doc);
        assert.strictEqual(doc.field, N + 1);

        update["$set"].field = N + 2;
        result = await collection.updateMany({}, update);
        assert.strictEqual(result.modifiedCount, N);

        var cursor = collection.find();

        await cursor.forEach(function(doc) {
            assert.strictEqual(doc.field, N + 2);
        });
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
