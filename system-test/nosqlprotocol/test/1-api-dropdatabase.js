/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// https://docs.mongodb.com/manual/reference/command/dropDatabase

const assert = require('assert');
const test = require('./nosqltest')

const name = "dropDatabase";

describe(name, function () {
    this.timeout(test.timeout);

    let mng;
    let mxs;
    let conn;

    var random_db = "db" + Math.random().toString(10).substring(2);

    /*
     * MOCHA
     */
    before(async function () {
        mng = await test.MDB.create(test.MngMongo, random_db);
        mxs = await test.MDB.create(test.MxsMongo, random_db);
    });

    it('Can drop non-existing database.', async function () {
        var rv1 = await mng.runCommand({dropDatabase: 1});
        var rv2 = await mxs.runCommand({dropDatabase: 1});

        assert.equal(rv1.ok, 1);
        assert.deepEqual(rv1, rv2);
    });

    it('Can drop existing database.', async function () {
        conn = await test.MariaDB.createConnection();

        await conn.query("CREATE DATABASE " + random_db);

        await mng.insert_n(name, 5);
        await mxs.insert_n(name, 5);

        var rv1 = await mng.find(name);
        var rv2 = await mxs.find(name);

        assert.equal(rv1.cursor.firstBatch.length, 5);
        assert.equal(rv2.cursor.firstBatch.length, 5);

        rv1 = await mng.runCommand({dropDatabase: 1});
        rv2 = await mxs.runCommand({dropDatabase: 1});

        assert.equal(rv1.ok, 1);
        assert.deepEqual(rv1, rv2);

        var rv1 = await mng.find(name);
        var rv2 = await mxs.find(name);

        assert.equal(rv1.cursor.firstBatch.length, 0);
        assert.equal(rv2.cursor.firstBatch.length, 0);

        try {
            await conn.query("SHOW TABLES FROM " + random_db);
        }
        catch (x)
        {
            if (x.errno != 1049) { // Unknown database
                throw x;
            }
        }
    });

    after(async function () {
        if (mxs) {
            await mxs.close();
        }

        if (mng) {
            await mng.close();
        }

        if (conn) {
            await conn.end();
        }
    });
});
