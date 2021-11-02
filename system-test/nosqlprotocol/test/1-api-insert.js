/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// https://docs.mongodb.com/manual/reference/command/insert/

const assert = require('assert');
const test = require('./nosqltest');
const error = test.error;
const mongodb = test.mongodb;

const name = "insert";

describe(name, function () {
    this.timeout(test.timeout);

    let mng;
    let mxs;
    let conn;

    const dbname = "insertdb";
    const tblname = "inserttbl";

    /*
     * HELPERS
     */
    async function reset(dbname, tblname)
    {
        await mng.set_db(dbname);
        await mxs.set_db(dbname);

        await mng.deleteAll(tblname);
        await mxs.deleteAll(tblname);
    }

    /*
     * MOCHA
     */
    before(async function () {
        mxs = await test.MDB.create(test.MxsMongo);
        mng = await test.MDB.create(test.MngMongo);
        conn = await test.MariaDB.createConnection();
    });

    it('Can insert in existing collection/table.', async function () {
        await mng.ntRunCommand({drop: tblname});
        await mxs.ntRunCommand({drop: tblname});

        await mng.runCommand({create: tblname});
        await mxs.runCommand({create: tblname});

        await mng.ntRunCommand({drop: tblname});

        var doc = { hello: "world" };
        var command = { insert: tblname, documents: [ doc ] };

        var rv1 = await mng.runCommand(command);
        var rv2 = await mxs.runCommand(command);

        assert.deepEqual(rv2, rv1);

        rv1 = await mng.find(tblname);
        rv2 = await mxs.find(tblname);

        assert.equal(rv1.cursor.firstBatch.length, 1);
        assert.equal(rv2.cursor.firstBatch.length, 1);
    });

    it('Can insert in non-existing collection/table; auto_create_tables ON.', async function () {
        await conn.query("DROP TABLE IF EXISTS test." + tblname);

        // First we ensure the auto creation is on.
        var config = { auto_create_tables: true };
        rv = await mxs.adminCommand({mxsSetConfig: config});
        assert.equal(rv.ok, 1);

        var doc = { hello: "world" };
        var command = { insert: tblname, documents: [ doc ] };

        var rv = await mxs.runCommand(command);
        assert.equal(rv.ok, 1);
        assert.equal(rv.n, 1);
    });

    it('Cannot insert in non-existing collection/table, with auto_create_tables OFF.', async function () {
        // NOTE: Using dbname, not 'test'.
        await conn.query("DROP TABLE IF EXISTS test." + tblname);

        // First we ensure the auto creation is off.
        var config = { auto_create_tables: false };
        rv = await mxs.adminCommand({mxsSetConfig: config});
        assert.equal(rv.ok, 1);

        var doc = { hello: "world" };
        var command = { insert: tblname, documents: [ doc ] };

        var rv = await mxs.ntRunCommand(command);
        assert.equal(rv.code, error.COMMAND_FAILED);
    });

    it('Can insert in non-existing database; auto_create_[databases|tables] [ON|ON].', async function () {
        await conn.query("DROP DATABASE IF EXISTS " + dbname);

        mxs.set_db(dbname);

        // First we ensure the auto creation is on.
        var config = {
            auto_create_databases: true,
            auto_create_tables: true
        };
        rv = await mxs.adminCommand({mxsSetConfig: config});
        assert.equal(rv.ok, 1);

        var doc = { hello: "world" };
        var command = { insert: tblname, documents: [ doc ] };

        var rv = await mxs.runCommand(command);
        assert.equal(rv.ok, 1);
        assert.equal(rv.n, 1);
    });

    it('Cant insert in non-existing database; auto_create_[databases|tables] = [OFF|ON].', async function () {
        // NOTE: Using dbname, not 'test'.
        await conn.query("DROP DATABASE IF EXISTS " + dbname);

        // First we ensure the auto creation is on.
        var config = {
            auto_create_databases: false,
            auto_create_tables: true
        };
        rv = await mxs.adminCommand({mxsSetConfig: config});
        assert.equal(rv.ok, 1);

        var doc = { hello: "world" };
        var command = { insert: tblname, documents: [ doc ] };

        var rv = await mxs.ntRunCommand(command);

        assert.equal(rv.code, error.COMMAND_FAILED);
    });

    it('Can insert many.', async function () {
        await reset('test', tblname);

        var n = 997;
        var documents = [];
        for (var i = 0; i < n; ++i) {
            documents.push({i: i});
        }

        var rv = await mxs.runCommand({insert: tblname, documents: documents});

        assert.equal(rv.ok, 1);
        assert.equal(rv.n, n);
    });

    it('Can insert with specified id.', async function() {
        await reset('test', tblname);

        var documents = [
            { _id: 1 },
            { _id: "hello" },
            { _id: mongodb.ObjectId() }
        ];

        var rv1 = await mng.runCommand({insert: tblname, documents: documents});
        var rv2 = await mxs.runCommand({insert: tblname, documents: documents});

        assert.deepEqual(rv1, rv2);
    });

    it('Can behave in default way.', async function () {
        await reset('test', tblname);

        var config = {
            ordered_insert_behavior: "default"
        };

        await mxs.adminCommand({mxsSetConfig: config});

        var documents = [
            { _id: 1 },
            { _id: 2 },
            { _id: 3 },
            { _id: 4 },
            { _id: 5 },
            { _id: 1 }, // Duplicate
            { _id: 6 },
        ];

        var command = {insert: tblname, documents: documents, ordered: true };

        var rv1 = await mng.runCommand(command);
        var rv2 = await mxs.runCommand(command);

        // Ordered is true, so only the 5 first should be inserted.
        assert.equal(rv1.n, 5);
        assert.equal(rv2.n, 5);

        await reset('test', tblname);

        var command = {insert: tblname, documents: documents, ordered: false };

        var rv1 = await mng.runCommand(command);
        var rv2 = await mxs.runCommand(command);

        // Ordered is false, so the 5 first and the last should be inserted.
        assert.equal(rv1.n, 6);
        assert.equal(rv2.n, 6);
    });

    it('Can behave in atomic way.', async function () {
        await reset('test', tblname);

        var config = {
            ordered_insert_behavior: "atomic"
        };

        await mxs.adminCommand({mxsSetConfig: config});

        var documents = [
            { _id: 1 },
            { _id: 2 },
            { _id: 3 },
            { _id: 4 },
            { _id: 5 },
            { _id: 1 }, // Duplicate
            { _id: 6 },
        ];

        var command = {insert: tblname, documents: documents, ordered: true };

        var rv = await mxs.ntRunCommand(command);

        // Insert behaviour is atomic and ordered is true, so no documents should be inserted.
        assert.equal(rv.n, 0);

        await reset('test', tblname);

        var command = {insert: tblname, documents: documents, ordered: false };

        var rv = await mxs.runCommand(command);

        // Ordered is false, so the 5 first and the last should be inserted.
        assert.equal(rv.n, 6);
    });

    it('Can insert single quote.', async function () {
        await mxs.runCommand({insert: tblname, documents: [{a: "a'b"}]});
        await mxs.runCommand({insert: tblname, documents: [{a: "a\b"}]});
        await mxs.runCommand({insert: tblname, documents: [{a: "a\'b"}]});
        await mxs.runCommand({insert: tblname, documents: [{a: "a\\''b"}]});
    });

    after(function () {
        if (mxs) {
            mxs.close();
        }

        if (mng) {
            mng.close();
        }

        if (conn) {
            conn.end();
        }
    });
});
