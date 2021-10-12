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

// https://docs.mongodb.com/manual/reference/command/delete/

const test = require('./nosqltest');

const config = test.config;

const mariadb = test.mariadb;
const mongodb = test.mongodb;
const assert = test.assert;

describe('delete', function () {
    this.timeout(test.timeout);

    let mng;
    let mxs;
    let mngdb;
    let mxsdb;

    const name = "delete";

    /*
     * HELPERS
     */
    async function reset_db(db) {
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

    async function reset() {
        await reset_db(mngdb);
        await reset_db(mxsdb);
    }

    async function del_db(db, deletes, ordered) {
        var command = {"delete": name, deletes: deletes};

        if (ordered != undefined)
        {
            command.ordered = ordered;
        }

        return await db.command(command);
    }

    async function del(deletes, ordered) {
        var mngrv = await del_db(mngdb, deletes, ordered);
        var mxsrv = await del_db(mxsdb, deletes, ordered);

        assert.deepEqual(mngrv, mxsrv);

        return mxsrv;
    }

    async function insert_db(db, documents)
    {
        var command = {
            insert: name,
            documents: documents
        };

        return await db.command(command);
    }

    async function insert_n(n)
    {
        var documents = [];
        for (i = 0; i < n; ++i) {
            var doc = { i: i};
            documents.push(doc);
        }

        var mngrv = await insert_db(mngdb, documents);
        var mxsrv = await insert_db(mxsdb, documents);

        assert.deepEqual(mngrv, mxsrv);
    }

    /*
     * MOCHA
     */
    before(async function () {
        mxs = await test.MxsMongo.createClient();
        mng = await test.MngMongo.createClient();

        mxsdb = mxs.db("test");
        mngdb = mng.db("test");

        await reset();
    });

    it('Delete deletes data from a non-existent collection', async function () {
        await del([{q:{}, limit:1}]);
    });

    it('Delete honours limit when deleting data from collection', async function () {
        await insert_n(50);

        var rv;

        rv = await del([{q:{}, limit:1}]); // Delete one

        assert.equal(rv.ok, 1);
        assert.equal(rv.n, 1);

        rv = await del([{q:{}, limit:0}]); // Delete all

        assert.equal(rv.ok, 1);
        assert.equal(rv.n, 49);
    });

    it('Delete deletes according to query', async function () {
        await insert_n(50);

        var q = { $and: [{i: { $gt: 19}}, {i: {$lt: 28}}] };

        var rv;

        rv = await del([{q:q, limit:0}]);

        assert.equal(rv.ok, 1);
        assert.equal(rv.n, 8);

        rv = await mxsdb.command({find: name});

        assert.equal(rv.cursor.firstBatch.length, 42);

        var q1 = { $and: [{i: { $gte: 5}}, {i: {$lt: 8}}] };
        var q2 = { $and: [{i: { $gte: 30}}, {i: {$lt: 38}}] };

        rv = await del([{q:q1, limit: 0}, {q:q2, limit: 0}]);

        assert.equal(rv.ok, 1);
        assert.equal(rv.n, (8 - 5) + (38 - 30));
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
