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

// https://docs.mongodb.com/manual/reference/command/find/

const assert = require('assert');
const test = require('./mongotest');
const error = test.error;
const mongodb = test.mongodb;

const name = "find";
const misc = "misc";

describe(name, function () {
    this.timeout(test.timeout);

    let mng;
    let mxs;

    const I = 5;
    const J = 20;
    const K = 100;

    const N = 100;

    /*
     * HELPERS
     */
    async function drop(n) {
        if (!n) {
            n = name;
        }

        var command = {
            drop: n
        };

        await mng.ntRunCommand(command);
        await mxs.ntRunCommand(command);
    }

    async function insert() {
        var documents = [];

        var i = 0;
        var j = 0;
        var k = 0;

        for (var n = 0; n < N; ++n) {
            var doc = {
                i: i,
                j: j,
                k: k
            };

            ++i;
            if (i == I) {
                i = 0;
            }

            ++j;
            if (j == J) {
                j = 0;
            }

            ++k;
            if (k == K) {
                k = 0;
            }

            doc._id = k;

            documents.push(doc);
        }

        await mng.runCommand({insert: name, documents: documents});
        await mxs.runCommand({insert: name, documents: documents});
    }

    async function deleteAll() {
        await mng.deleteAll(name);
        await mxs.deleteAll(name);
    }

    async function find_with_comparison_query_operator(field, op, value, expected) {
        var comparison = {};
        comparison[op] = value;

        var filter = {};
        filter[field] = comparison;

        var command = {
            find: name,
            filter: filter
        };

        var rv1 = await mng.runCommand(command);
        var rv2 = await mxs.runCommand(command);

        assert.equal(rv1.cursor.firstBatch.length, expected);
        assert.equal(rv2.cursor.firstBatch.length, expected);
    }

    /*
     * MOCHA
     */
    before(async function () {
        mng = await test.MDB.create(test.MngMongo);
        mxs = await test.MDB.create(test.MxsMongo);

        await drop();
        await insert();
    });

    it('Finds all.', async function () {
        var command = {
            find: name
        };

        var rv1 = await mng.runCommand(command);
        var rv2 = await mxs.runCommand(command);

        assert.equal(rv1.cursor.firstBatch.length, N);
        assert.equal(rv2.cursor.firstBatch.length, N);
    });

    it('Finds with $eq', async function () {
        await find_with_comparison_query_operator("i", "$eq", 4, N / I);
        await find_with_comparison_query_operator("j", "$eq", 17, N / J);
        await find_with_comparison_query_operator("k", "$eq", 3, N / K);

        await find_with_comparison_query_operator("i", "$eq", N, 0);
        await find_with_comparison_query_operator("j", "$eq", N, 0);
        await find_with_comparison_query_operator("k", "$eq", N, 0);
    });

    it('Finds with $gt', async function () {
        await find_with_comparison_query_operator("i", "$gt", 2, (4 - 2) * N / I);
        await find_with_comparison_query_operator("j", "$gt", 15, (19 - 15) * N / J);
        await find_with_comparison_query_operator("k", "$gt", 49, (99 - 49) * N / K);

        await find_with_comparison_query_operator("i", "$gt", N, 0);
        await find_with_comparison_query_operator("j", "$gt", N, 0);
        await find_with_comparison_query_operator("k", "$gt", N, 0);
    });

    it('Finds with $gte', async function () {
        await find_with_comparison_query_operator("i", "$gte", 2, (1 + 4 - 2) * N / I);
        await find_with_comparison_query_operator("j", "$gte", 15, (1 + 19 - 15) * N / J);
        await find_with_comparison_query_operator("k", "$gte", 49, (1 + 99 - 49) * N / K);

        await find_with_comparison_query_operator("i", "$gte", N, 0);
        await find_with_comparison_query_operator("j", "$gte", N, 0);
        await find_with_comparison_query_operator("k", "$gte", N, 0);
    });

    it('Finds with $in', async function () {
        await find_with_comparison_query_operator("i", "$in", [2, 4], 2 * N / I);
        await find_with_comparison_query_operator("j", "$in", [9, 11, 13], 3 * N / J);
        await find_with_comparison_query_operator("k", "$in", [9, 11, 13, 73], 4);

        await find_with_comparison_query_operator("i", "$in", [N], 0);
        await find_with_comparison_query_operator("j", "$in", [N, N + 1], 0);
        await find_with_comparison_query_operator("k", "$in", [N, N + 1, -1], 0);
    });

    it('Finds with $lt', async function () {
        await find_with_comparison_query_operator("i", "$lt", 2, 2 * N / I);
        await find_with_comparison_query_operator("j", "$lt", 15, 15 * N / J);
        await find_with_comparison_query_operator("k", "$lt", 49, 49 * N / K);

        await find_with_comparison_query_operator("i", "$lt", 0, 0);
        await find_with_comparison_query_operator("j", "$lt", 0, 0);
        await find_with_comparison_query_operator("k", "$lt", 0, 0);
    });

    it('Finds with $lte', async function () {
        await find_with_comparison_query_operator("i", "$lte", 2, 3 * N / I);
        await find_with_comparison_query_operator("j", "$lte", 15, 16 * N / J);
        await find_with_comparison_query_operator("k", "$lte", 49, 50 * N / K);

        await find_with_comparison_query_operator("i", "$lte", 0, N / I);
        await find_with_comparison_query_operator("j", "$lte", 0, N / J);
        await find_with_comparison_query_operator("k", "$lte", 0, N / K);
    });

    it('Finds with $ne', async function () {
        await find_with_comparison_query_operator("i", "$ne", 4, N - N / I);
        await find_with_comparison_query_operator("j", "$ne", 17, N - N / J);
        await find_with_comparison_query_operator("k", "$ne", 3, N - N / K);

        await find_with_comparison_query_operator("i", "$ne", N, N);
        await find_with_comparison_query_operator("j", "$ne", N, N);
        await find_with_comparison_query_operator("k", "$ne", N, N);
    });

    it('Finds with $nin', async function () {
        await find_with_comparison_query_operator("i", "$nin", [3], (I - 1) * N / I);
        await find_with_comparison_query_operator("j", "$nin", [13, 14, 16, 19], (J - 4) * N / J);
        await find_with_comparison_query_operator("k", "$nin", [1, 12, 25, 37, 48, 59, 60], K - 7);

        await find_with_comparison_query_operator("i", "$nin", [N + 1], N);
        await find_with_comparison_query_operator("j", "$nin", [N + 1, N + 2], N);
        await find_with_comparison_query_operator("k", "$nin", [N + 1, N + 2, N + 3], N);
    });

    it('Rejects invalid $in value.', async function () {
        var rv1 = await mng.ntRunCommand({find: name, filter: { "i": { "$in": 1 }}});
        assert.equal(rv1.code, error.BAD_VALUE);

        var rv2 = await mxs.ntRunCommand({find: name, filter: { "i": { "$in": 1 }}});
        assert.equal(rv2.code, error.BAD_VALUE);
    });

    it('Rejects invalid $nin value.', async function () {
        var rv1 = await mng.ntRunCommand({find: name, filter: { "i": { "$nin": 1 }}});
        assert.equal(rv1.code, error.BAD_VALUE);

        var rv2 = await mxs.ntRunCommand({find: name, filter: { "i": { "$nin": 1 }}});
        assert.equal(rv2.code, error.BAD_VALUE);
    });

    it('Can use arrays as values.', async function () {
        await drop(misc);

        var documents = [
            { a: [ 1, 2, 3 ] },
            { a: [ 2, 3, 4 ] },
            { a: [ 5, 6, 7 ] },
        ];

        await mng.runCommand({insert: misc, documents: documents});
        await mxs.runCommand({insert: misc, documents: documents});

        var filter = {
            a: {
                "$eq": [2, 3, 4]
            }
        };

        var rv1 = await mng.runCommand({find: misc, filter: filter});
        assert.equal(rv1.cursor.firstBatch.length, 1);
        assert.equal(rv1.cursor.firstBatch[0].a[0], 2);

        var rv2 = await mxs.runCommand({find: misc, filter: filter});
        assert.equal(rv2.cursor.firstBatch.length, 1);
        assert.equal(rv2.cursor.firstBatch[0].a[0], 2);
    });

    it('Can use objects as values.', async function () {
        await drop(misc);

        var documents = [
            { a: { b: 1 }},
            { a: { b: 2 }},
            { a: { b: 3 }}
        ];

        await mng.runCommand({insert: misc, documents: documents});
        await mxs.runCommand({insert: misc, documents: documents});

        var filter = {
            a: {
                "$eq": { b: 2 }
            }
        };

        var rv1 = await mng.runCommand({find: misc, filter: filter});
        assert.equal(rv1.cursor.firstBatch.length, 1);
        assert.equal(rv1.cursor.firstBatch[0].a.b, 2);

        var rv2 = await mxs.runCommand({find: misc, filter: filter});
        assert.equal(rv2.cursor.firstBatch.length, 1);
        assert.equal(rv2.cursor.firstBatch[0].a.b, 2);
    });

    it('Supports array dot notation', async function () {
        await drop(misc);

        var documents = [
            { a: [ 1, 2, 3 ] , _id: 1 },
            { a: [ 2, 3, 4 ] , _id: 2 },
            { a: [ 5, 6, 7 ] , _id: 3 }
        ];

        await mng.runCommand({insert: misc, documents: documents});
        await mxs.runCommand({insert: misc, documents: documents});

        var filter = {
            "a.1" : 3
        }

        var rv1 = await mng.runCommand({find: misc, filter: filter});
        assert.equal(rv1.cursor.firstBatch.length, 1);
        assert.equal(rv1.cursor.firstBatch[0].a[1], 3);

        var rv2 = await mxs.runCommand({find: misc, filter: filter});
        assert.deepEqual(rv1.cursor.firstBatch, rv2.cursor.firstBatch);
    });

    // In the case of the logical operators, we do not figure out what we actually
    // expect, but trust that MongoDB does the right thing and expect MxsMongo to do
    // the same. We do ensure that something must be returned.

    it('Supports $and', async function () {
        var filter = {
            $and: [
                { i: { $eq: 2 }},
                { j: { $gt: 10 }},
                { k: { $lt: 90 }}
            ]
        };

        var rv1 = await mng.runCommand({find: name, filter: filter });
        assert.notEqual(rv1.cursor.firstBatch.length, 0);

        var rv2 = await mxs.runCommand({find: name, filter: filter });

        assert.deepEqual(rv1.cursor, rv2.cursor);
    });

    it('Supports $nor', async function () {
        var filter = {
            $nor: [
                { i: 2, },
                { j: 10 },
                { k: 90 }
            ]
        };
        var rv1 = await mng.runCommand({find: name, filter: filter });
        assert.notEqual(rv1.cursor.firstBatch.length, 0);

        var rv2 = await mxs.runCommand({find: name, filter: filter });

        assert.deepEqual(rv1.cursor, rv2.cursor);
    });

    it('Supports $not', async function () {
        var filter = {
            i: { $not: { $gt: 3 }}
        };

        var rv1 = await mng.runCommand({find: name, filter: filter });
        assert.notEqual(rv1.cursor.firstBatch.length, 0);

        var rv2 = await mxs.runCommand({find: name, filter: filter });

        assert.deepEqual(rv1.cursor, rv2.cursor);

        var filter = {
            l: { $not: { $gt: 3 }} // Note: 'l' does not exist, so we should get all.
        };

        var rv1 = await mng.runCommand({find: name, filter: filter });
        assert.equal(rv1.cursor.firstBatch.length, N);

        var rv2 = await mxs.runCommand({find: name, filter: filter });

        assert.deepEqual(rv1.cursor, rv2.cursor);
    });

    it('Supports $or', async function () {
        var filter = {
            $or: [
                { i: { $eq: 3 }},
                { j: 5 },
                { k: { $gt: 37 }}
            ]
        };

        var rv1 = await mng.runCommand({find: name, filter: filter });
        assert.notEqual(rv1.cursor.firstBatch.length, 0);

        var rv2 = await mxs.runCommand({find: name, filter: filter });

        assert.deepEqual(rv1.cursor, rv2.cursor);
    });

    it('Supports $exists', async function () {
        var filter = {
            i: { $exists: true }
        };

        var rv1 = await mng.runCommand({find: name, filter: filter });
        assert.notEqual(rv1.cursor.firstBatch.length, 0);

        var rv2 = await mxs.runCommand({find: name, filter: filter });

        assert.deepEqual(rv1.cursor, rv2.cursor);

        var filter = {
            i: { $exists: true, $gt: 3 }
        };

        var rv1 = await mng.runCommand({find: name, filter: filter });
        assert.notEqual(rv1.cursor.firstBatch.length, 0);

        var rv2 = await mxs.runCommand({find: name, filter: filter });

        assert.deepEqual(rv1.cursor, rv2.cursor);
    });

    after(function () {
        drop(misc);
        drop(name);

        mng.close();
        mxs.close();
    });
});
