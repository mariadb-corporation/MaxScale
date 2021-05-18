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

// https://docs.mongodb.com/drivers/node/usage-examples/deleteOne/

const mariadb = require('mariadb');
const { MongoClient } = require('mongodb');

const assert = require('assert');

var host = process.env.maxscale_000_network;
var mariadb_port = 4008;
var mongodb_port = 17017;
var user = 'maxskysql';
var password = 'skysql';

before(async function () {
    if (!process.env.maxscale_000_network) {
        throw new Error("The environment variable 'maxscale_000_network' must be set.");
    }
});

describe('DELETE', function () {
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

    it('deletes one document', async function () {
        const original = { hello: "world" };

        var result = await collection.insertOne(original);
        assert.strictEqual(result.insertedCount, 1);

        var rows = await conn.query("SELECT * FROM test.mongo");
        assert.strictEqual(rows.length, 1);

        result = await collection.deleteOne();
        assert.strictEqual(result.deletedCount, 1);

        rows = await conn.query("SELECT * FROM test.mongo");
        assert.strictEqual(rows.length, 0);
    });

    it('deletes many documents', async function () {
        const originals = [
            { hello1: "world1" },
            { hello2: "world2" },
            { hello3: "world3" } ];

        var result = await collection.insertMany(originals);
        assert.strictEqual(result.insertedCount, originals.length);

        var rows = await conn.query("SELECT * FROM test.mongo");
        assert.strictEqual(rows.length, originals.length);

        result = await collection.deleteMany();
        assert.strictEqual(result.deletedCount, originals.length);

        rows = await conn.query("SELECT * FROM test.mongo");
        assert.strictEqual(rows.length, 0);
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
