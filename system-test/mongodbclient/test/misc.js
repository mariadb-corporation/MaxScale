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
const mongodb = require('mongodb')
const MongoClient = mongodb.MongoClient;

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

    const valid_ids = [ "blah", 42, mongodb.ObjectID() ];

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

    after(function () {
        if (client) {
            client.close();
        }

        if (conn) {
            conn.end();
        }
    });
});
