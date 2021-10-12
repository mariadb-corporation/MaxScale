/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// https://docs.mongodb.com/manual/reference/command/renameCollection

const assert = require('assert');
const test = require('./nosqltest')

const name = "renameCollection";

describe(name, function () {
    this.timeout(test.timeout);

    let admin;
    let mxs;
    let conn;

    var dbname1 = name + "_1";
    var dbname2 = name + "_2";

    /*
     * MOCHA
     */
    before(async function () {
        admin = await test.MDB.create(test.MxsMongo, "admin");

        conn = await test.MariaDB.createConnection();
    });

    it('Can rename collection in same database.', async function () {
        // First delete the db.
        await conn.query("DROP DATABASE IF EXISTS " + dbname1);

        // Then create it.
        await admin.runCommand({mxsCreateDatabase: dbname1});

        // And get a db handle to it.
        mxs = await test.MDB.create(test.MxsMongo, dbname1);

        // Insert elements in collection 'coll1'.
        await mxs.insert_n("coll1", 50);

        // Check that they are found.
        var rv = await mxs.find("coll1");
        assert.equal(rv.cursor.firstBatch.length, 50);

        var from = dbname1 + ".coll1";
        var to = dbname1 + ".coll2";

        // Rename 'coll1' to 'coll2'.
        rv = await admin.runCommand({renameCollection: from, to: to});
        assert.equal(rv.ok, 1);

        // Check that nothing is found using the old name.
        rv = await mxs.find("coll1");
        assert.equal(rv.cursor.firstBatch.length, 0);

        // And that everything is found using the new name.
        rv = await mxs.find("coll2");
        assert.equal(rv.cursor.firstBatch.length, 50);

        await mxs.close();
    });

    it('Can rename collection from one database to another.', async function () {
        // First delete the dbs.
        await conn.query("DROP DATABASE IF EXISTS " + dbname1);
        await conn.query("DROP DATABASE IF EXISTS " + dbname2);

        // Then create them.
        await admin.runCommand({mxsCreateDatabase: dbname1});
        await admin.runCommand({mxsCreateDatabase: dbname2});

        // And get a db handle to it.
        mxs = await test.MDB.create(test.MxsMongo, dbname1);

        // Insert elements in collection 'coll1'.
        await mxs.insert_n("coll1", 50);

        // Check that they are found.
        var rv = await mxs.find("coll1");
        assert.equal(rv.cursor.firstBatch.length, 50);

        var from = dbname1 + ".coll1";
        var to = dbname2 + ".coll2";

        // Rename 'coll1' to 'coll2'.
        rv = await admin.runCommand({renameCollection: from, to: to});
        assert.equal(rv.ok, 1);

        // Check that nothing is found using the old name.
        rv = await mxs.find("coll1");
        assert.equal(rv.cursor.firstBatch.length, 0);

        // And that everything is found using the new name.
        mxs.close();
        mxs = await test.MDB.create(test.MxsMongo, dbname2);

        rv = await mxs.find("coll2");
        assert.equal(rv.cursor.firstBatch.length, 50);
    });

    after(async function () {
        if (admin) {
            await admin.close();
        }

        if (mxs) {
            await mxs.close();
        }

        if (conn) {
            await conn.end();
        }
    });
});
