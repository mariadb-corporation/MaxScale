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

// https://docs.mongodb.com/manual/reference/command/count/

const test = require('./mongotest');

const config = test.config;

const mariadb = test.mariadb;
const mongodb = test.mongodb;
const assert = test.assert;

describe('count', function () {
    this.timeout(test.timeout);

    let mng;
    let mxs;
    let mngdb;
    let mxsdb;

    const name = "count";

    /*
     * HELPERS
     */
    async function count(db, nExpected, skip, limit, query) {
        var command = {count: name};

        if (skip) {
            command.skip = skip;
        }

        if (limit) {
            command.limit = limit;
        };

        if (query) {
            command.query = query;
        }

        var rv = await mxsdb.command(command);

        assert.equal(rv.ok, 1);
        assert.equal(rv.n, nExpected);
    }

    async function count_both(nExpected, skip, limit, query) {
        await count(mngdb, nExpected, skip, limit, query);
        await count(mxsdb, nExpected, skip, limit, query);
    }

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

    async function insert_both(m, n) {
        await insert(mngdb, m, n);
        await insert(mxsdb, m, n);
    }

    /*
     * MOCHA
     */
    before(async function () {
        mxs = await test.MxsMongo.createClient();
        mng = await test.MngMongo.createClient();

        mxsdb = mxs.db("test");
        mngdb = mng.db("test");

        async function prepare(db) {
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

        await prepare(mngdb);
        await prepare(mxsdb);
    });

    it('Counts correctly a non-existent collection', async function () {
        await count_both(0);
    });

    it('Counts correctly a single-element collection', async function () {
        await insert_both(0, 1);

        await count_both(1);
    });

    it('Counts correctly a multi-element collection', async function () {
        var n = 59;

        await insert_both(1, n);

        await count_both(n);
    });

    it('Counts correctly with skip', async function () {
        var n = 59;
        var skip = 7;

        var nExpected = n - skip;
        await count_both(nExpected, skip);
    });

    it('Counts correctly with limit', async function () {
        var limit = 51;

        // If there are more items than the limit, then we will count the limit.
        var nExpected = limit;
        await count_both(nExpected, undefined, limit);
    });

    it('Counts correctly with skip and limit', async function () {
        var n = 59;
        var skip = 20;
        var limit;
        var nExpected;

        // If we start counting at a point where there are fewer that the limit left,
        // then we will count what's left.
        limit = 50;
        nExpected = n - skip;
        await count_both(nExpected, skip, limit);

        // If we start counting at a point where there are fewer that the limit left,
        // then we will count the limit.
        limit = 10;
        nExpected = limit;
        await count_both(nExpected, skip, limit);
    });

    it('Counts correctly with where', async function () {
        var nExpected;
        var skip = undefined;
        var limit = undefined;
        var query = { $and: [{i: { $lt: 30}}, {i: { $gte: 20}}]};

        // 20 <= i < 30 => 10
        nExpected = 10;
        await count_both(nExpected, skip, limit, query);

        // 10, but starting from 5 => 5
        skip = 5;
        nExpected = 5;
        await count_both(nExpected, skip, limit, query);

        // 10, but starting from 7 (=> 3), but limiting the result to 2.
        skip = 7;
        limit = 2;
        nExpected = 2;
        await count_both(nExpected, skip, limit, query);
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
