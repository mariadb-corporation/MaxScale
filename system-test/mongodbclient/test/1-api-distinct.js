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

// https://docs.mongodb.com/manual/reference/command/distinct/

const test = require('./mongotest');

const config = test.config;

const mariadb = test.mariadb;
const mongodb = test.mongodb;
const assert = test.assert;

describe('distinct', function () {
    let mng;
    let mxs;
    let mngdb;
    let mxsdb;

    const name = "distinct";

    /*
     * HELPERS
     */
    async function prepare_db(db) {
        try {
            await db.command({ drop: name});
        }
        catch (x)
        {
            if (x.code != 26) // NameSpace not found
            {
                throw x;
            }
        }
    }

    async function prepare() {
        await prepare_db(mngdb);
        await prepare_db(mxsdb);
    }

    async function distinct_db(db, key, query) {
        var command = {distinct: name, key: key};

        if (query)
        {
            command.query = query;
        }

        return await db.command(command);
    }

    async function distinct(key, n, query) {
        var mngrv = await distinct_db(mngdb, key, query);
        var mxsrv = await distinct_db(mxsdb, key, query);

        assert.deepEqual(mngrv, mxsrv);

        if (n != undefined)
        {
            assert.equal(n, mngrv.values.length);
        }
    }

    /*
     * MOCHA
     */
    before(async function () {
        mxs = await test.MxsMongo.createClient();
        mng = await test.MngMongo.createClient();

        mxsdb = mxs.db("test");
        mngdb = mng.db("test");

        prepare();
    });

    it('Distincts correctly a non-existent collection', async function () {
        await distinct('dummy');
    });

    it('Distincts top-level field', async function () {
        async function insert(db, m, n) {
            var documents = [];

            for (var i = m; i < n; ++i) {
                var doc = { i: i };
                documents.push(doc);
            }

            var rv = await db.command({insert: name, documents: documents});

            assert.equal(rv.ok, 1);
            assert.equal(rv.n, n - m);
        }

        // Let's add 50 documents, i-field goes from 0 - 49.
        await insert(mngdb, 0, 50);
        await insert(mxsdb, 0, 50);

        await distinct('i', 50);

        // Let's add 5 documents, i-field goes from 20-24
        await insert(mngdb, 20, 25);
        await insert(mxsdb, 20, 25);

        // There should still be 50 distinct values.
        await distinct('i', 50);
    });

    it('Distincts nested-level field', async function () {
        async function insert(db, m, n) {
            var documents = [];

            for (var i = m; i < n; ++i) {
                var doc = { j: { i: i } };
                documents.push(doc);
            }

            var rv = await db.command({insert: name, documents: documents});

            assert.equal(rv.ok, 1);
            assert.equal(rv.n, n - m);
        }

        prepare(); // Cleanup database

        // Let's add 50 documents, i-field goes from 0 - 49.
        await insert(mngdb, 0, 50);
        await insert(mxsdb, 0, 50);

        await distinct('j.i', 50);

        // Let's add 5 documents, i-field goes from 20-24
        await insert(mngdb, 20, 25);
        await insert(mxsdb, 20, 25);

        // There should still be 50 distinct values.
        await distinct('j.i', 50);
    });

    it('Distincts correctly with query', async function () {
        var query = { "j.i": 24 }; // There will be two of these documents.

        await distinct("j.i", 1, query);
    });

    after(function () {
        if (mxs) {
            mxs.close();
        }

        if (mng) {
            mng.close();
        }
    });
});
