/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// https://docs.mongodb.com/manual/reference/command/listDatabases

const assert = require('assert');
const test = require('./nosqltest')
const error = test.error;

const name = "listDatabases";

describe(name, function () {
    this.timeout(test.timeout);

    let mxs;
    let mng;
    let conn;

    /*
     * MOCHA
     */
    before(async function () {
        mng = await test.MDB.create(test.MngMongo, name);
        mxs = await test.MDB.create(test.MxsMongo, name);
        conn = await test.MariaDB.createConnection();
    });

    it('Cant list databases using non-admin.', async function () {
        var rv1 = await mng.ntRunCommand({"listDatabases": 1});
        var rv2 = await mxs.ntRunCommand({"listDatabases": 1});

        assert.equal(rv1.code, error.UNAUTHORIZED);
        assert.equal(rv2.code, error.UNAUTHORIZED);
    });

    it('Can list databases using admin.', async function () {
        mng.close();
        mxs.close();

        mng = await test.MDB.create(test.MngMongo, "admin");
        mxs = await test.MDB.create(test.MxsMongo, "admin");

        // First ensure the database we are going to create is not there.
        await conn.query("DROP DATABASE IF EXISTS " + name);

        var rv1 = await mng.runCommand({"listDatabases": 1});
        var rv2 = await mxs.runCommand({"listDatabases": 1});

        assert.equal(rv1.ok, 1);
        assert.equal(rv2.ok, 1);

        // How many databases are there?
        var n = rv2.databases.length;

        // Let's create another one.
        await conn.query("CREATE DATABASE " + name);

        rv2 = await mxs.runCommand({"listDatabases": 1});

        // There should now be one more.
        assert.equal(rv2.ok, 1);
        assert.equal(rv2.databases.length, n + 1);

        var database = rv2.databases[0];

        // 'nameOnly' was not specified, we should get all three fields.
        assert.notEqual(database.name, undefined);
        assert.notEqual(database.sizeOnDisk, undefined);
        assert.notEqual(database.empty, undefined);

        rv2 = await mxs.runCommand({"listDatabases": 1, nameOnly: true});

        assert.equal(rv2.ok, 1);
        assert.equal(rv2.databases.length, n + 1);

        var database = rv2.databases[0];

        // 'nameOnly' was specified, we should get only one field.
        assert.notEqual(database.name, undefined);
        assert.equal(database.sizeOnDisk, undefined);
        assert.equal(database.empty, undefined);

        await conn.query("DROP DATABASE " + name);
    });

    after(async function () {
        if (mng) {
            await mng.close();
        }

        if (mxs) {
            await mxs.close();
        }

        if (conn) {
            conn.end();
        }
    });
});
