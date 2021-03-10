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

describe('MISCALLENOUS', function () {
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
        await conn.query("CREATE TABLE mongo (id TEXT, doc JSON)");

        // MxsMongo
        var uri = "mongodb://" + host + ":" + mongodb_port;

        client = new MongoClient(uri, { useUnifiedTopology: true });
        await client.connect();

        const database = client.db("test");
        collection = database.collection("mongo");
    });

    it('Can insert using client specified id', async function () {
        var o = { hello: "world"};
        var result;

        o._id = "blah";
        result = await collection.insertOne(o);
        assert.strictEqual(result.insertedCount, 1, "Should be able to insert a document.");

        o._id = 42;
        result = await collection.insertOne(o);
        assert.strictEqual(result.insertedCount, 1, "Should be able to insert a document.");

        o._id = 3.14;
        result = await collection.insertOne(o);
        assert.strictEqual(result.insertedCount, 1, "Should be able to insert a document.");

        o._id = true;
        result = await collection.insertOne(o);
        assert.strictEqual(result.insertedCount, 1, "Should be able to insert a document.");

        o._id = null;
        result = await collection.insertOne(o);
        assert.strictEqual(result.insertedCount, 1, "Should be able to insert a document.");
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
