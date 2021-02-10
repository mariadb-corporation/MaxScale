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

describe('insertOne', function () {
    let conn;
    let client;
    let collection;

    before(async function () {
        // MariaDB
        conn = await mariadb.createConnection({
            host: host,
            port: mariadb_port,
            user: user,
            password: password });

        await conn.query("USE test");
        await conn.query("DROP TABLE IF EXISTS mongo");
        await conn.query("CREATE TABLE mongo (id TEXT, doc JSON)");

        // MxsMongo
        var uri = "mongodb://" + host + ":" + mongodb_port;

        client = new MongoClient(uri, { useUnifiedTopology: true });
        await client.connect();

        const database = client.db("test");
        collection = database.collection("mongo");
    });

    it('inserts one document', async function () {
        const original = { hello: "world" };
        const result = await collection.insertOne(original);

        assert.strictEqual(result.insertedCount, 1, "Should be able to insert a document.");

        var rows = await conn.query("SELECT * FROM test.mongo");

        assert.strictEqual(rows.length, 1, "One row should have been inserted.");

        var row = rows[0];

        var inserted = JSON.parse(row.doc);

        console.log("Inserted: ", inserted);
        delete original["_id"]; // insertOne() adds it.
        delete inserted["_id"]; // TODO: Check what real MongoDB returns.

        assert.deepStrictEqual(original, inserted, "Original and inserted objects should be identical");
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
