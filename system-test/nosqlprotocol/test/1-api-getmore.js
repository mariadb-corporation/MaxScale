/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// https://docs.mongodb.com/manual/reference/command/getMore

const test = require('./nosqltest');

const config = test.config;

const mariadb = test.mariadb;
const mongodb = test.mongodb;
const assert = test.assert;

describe('getMore', function () {
    this.timeout(test.timeout);

    let mng;
    let mxs;
    let mngdb;
    let mxsdb;

    const name = "getMore";

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

    async function reset()
    {
        await reset_db(mngdb);
        await reset_db(mxsdb);
    }

    async function get_more_db(db, cursor, collection, batch_size) {
        var command = {
            getMore: cursor,
            collection: collection
        }

        if (batch_size)
        {
            command.batchSize = batch_size;
        }

        return await db.command(command);
    }

    async function insert_db(db, n) {
        var documents = [];

        for (var i = 0; i < n; ++i) {
            var doc = { i: i };
            documents.push(doc);
        }

        var rv = await db.command({insert: name, documents: documents});

        assert.equal(rv.ok, 1);
        assert.equal(rv.n, n);
    }

    async function insert(n) {
        await insert_db(mngdb, n);
        await insert_db(mxsdb, n);
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

    it('getMore: Returns error for invalid cursor to non-existent collection.', async function () {
        var x1;
        var x1;

        try {
            await get_more_db(mngdb, mongodb.Long(4711), name);
            assert.ok(!true);
        }
        catch (x)
        {
            rv1 = x;
        }

        try {
            await get_more_db(mxsdb, mongodb.Long(4711), name);
            assert.ok(!true);
        }
        catch (x)
        {
            rv2 = x;
        }

        assert.deepEqual(rv1, rv2);
    });

    async function iterate(total, batchSize)
    {
        await reset();
        await insert(total);

        var n1 = 0;
        var n2 = 0;

        var rv1 = await mngdb.command({find: name, batchSize: batchSize});
        var rv2 = await mxsdb.command({find: name, batchSize: batchSize});

        assert.equal(rv1.cursor.firstBatch.length, rv2.cursor.firstBatch.length);

        n1 += rv1.cursor.firstBatch.length;
        n2 += rv2.cursor.firstBatch.length;

        var id1 = rv1.cursor.id;
        var id2 = rv2.cursor.id;

        while (id1 != 0 || id2 != 0)
        {
            if (id1) {
                rv1 = await mngdb.command({getMore: id1, collection: name, batchSize: batchSize});
                n1 += rv1.cursor.nextBatch.length;
                id1 = rv1.cursor.id;
            }

            if (id2) {
                rv2 = await mxsdb.command({getMore: id2, collection: name, batchSize: batchSize});
                n2 += rv2.cursor.nextBatch.length;
                id2 = rv2.cursor.id;
            }
        }

        assert.equal(id1, 0);
        assert.equal(id2, 0);
        assert.equal(n1, n2);
        assert.equal(n1, total);
    }

    it('getMore: Iterates correctly across documents.', async function () {
        await iterate(1, 1);
        await iterate(2, 1);
        await iterate(17, 1);
        await iterate(23, 3);
        await iterate(24, 8);
        await iterate(123, 17);
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
