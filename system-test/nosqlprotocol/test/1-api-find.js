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

// https://docs.mongodb.com/manual/reference/command/find/

const assert = require('assert');
const test = require('./nosqltest');
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

    it('Supports projection', async function () {
        var projection = {
            i: 1
        };

        var command = {
            find: name,
            projection: projection
        };

        var rv1 = await mng.runCommand(command);
        assert.notEqual(rv1.cursor.firstBatch.length, 0);
        assert.notEqual(rv1.cursor.firstBatch[0].i, undefined);
        assert.equal(rv1.cursor.firstBatch[0].j, undefined);
        assert.equal(rv1.cursor.firstBatch[0].k, undefined);

        var rv2 = await mng.runCommand(command);
        assert.deepEqual(rv1.cursor.firstBatch, rv2.cursor.firstBatch);
    });

    it('Supports skip and limit', async function () {
        var command = {
            find: name,
            skip: 7
        };

        // Supports skip
        var rv1 = await mng.runCommand(command);
        assert.notEqual(rv1.cursor.firstBatch.length, 0);

        var rv2 = await mng.runCommand(command);
        assert.deepEqual(rv1.cursor.firstBatch, rv2.cursor.firstBatch);

        // Supports skip and limit
        command.limit = 13;
        rv1 = await mng.runCommand(command);
        assert.notEqual(rv1.cursor.firstBatch.length, 0);

        rv2 = await mng.runCommand(command);
        assert.deepEqual(rv1.cursor.firstBatch, rv2.cursor.firstBatch);

        // Support limit
        delete command.skip;
        rv1 = await mng.runCommand(command);
        assert.notEqual(rv1.cursor.firstBatch.length, 0);

        rv2 = await mng.runCommand(command);
        assert.deepEqual(rv1.cursor.firstBatch, rv2.cursor.firstBatch);
    });

    it('Supports singleBatch', async function () {
        var command = {
            find: name,
            batchSize: 5,
            singleBatch: true
        };

        var rv1 = await mng.runCommand(command);
        assert.equal(rv1.cursor.firstBatch.length, command.batchSize);
        assert.equal(rv1.cursor.id, 0);

        var rv2 = await mxs.runCommand(command);
        assert.equal(rv2.cursor.firstBatch.length, command.batchSize);
        assert.equal(rv2.cursor.id, 0);
    });

    it('Can fetch with _id', async function () {
        drop(misc);

        var documents = [
            { _id: 4711 },
            { _id: "hello" },
            { _id: mongodb.ObjectId() }
        ];

        var command = {
            insert: misc,
            documents: documents
        };

        await mng.runCommand(command);
        await mxs.runCommand(command);

        for (var i in documents) {
            var doc = documents[i];

            var rv1 = await mng.runCommand({find: misc, filter: doc});
            assert.equal(rv1.cursor.firstBatch.length, 1);
            assert.deepEqual(rv1.cursor.firstBatch[0], doc);

            var rv2 = await mxs.runCommand({find: misc, filter: doc});
            assert.equal(rv2.cursor.firstBatch.length, 1);
            assert.deepEqual(rv2.cursor.firstBatch[0], doc);
        }
    });

    it('Supports $exists', async function () {
        await drop(misc);

        var documents = [
            { _id: 1, name: "bob", addresses: [ "Helsinki", "Turku" ] },
            { _id: 2, name: "alice", addresses: [ "Kotka", ] },
            { _id: 3, name: "cecil", addresses: [ "Oulu", "Kemi" ] }
        ];

        var command = {
            insert: misc,
            documents: documents
        };

        await mng.runCommand(command);
        await mxs.runCommand(command);

        command = {
            find: misc,
            filter: { "addresses.1": { $exists: true }}
        };

        var rv1 = await mng.runCommand(command);
        assert.equal(rv1.cursor.firstBatch.length, 2);
        assert.equal(rv1.cursor.firstBatch[0]._id, 1);
        assert.equal(rv1.cursor.firstBatch[1]._id, 3);

        var rv2 = await mxs.runCommand(command);
        assert.deepEqual(rv2.cursor.firstBatch, rv1.cursor.firstBatch);
    });

    it('Supports simple $elemMatch', async function () {
        await drop(misc);

        var documents = [
            { _id: 1, a: [1, 2, 3] },
            { _id: 2, a: [2, 3, 4] },
            { _id: 3, a: [3, 4, 5] },
            { _id: 4, a: [4, 5, 6] },
        ];

        var command = {
            insert: misc,
            documents: documents
        };

        await mng.runCommand(command);
        await mxs.runCommand(command);

        command = {
            find: misc
        };

        // Fetch all documents whose 'a' array contains 1 => 1
        command.filter = { a: { $elemMatch: { $eq: 1 }}}
        var rv1 = await mng.runCommand(command);
        var rv2 = await mxs.runCommand(command);
        assert.equal(rv1.cursor.firstBatch.length, 1);
        assert.deepEqual(rv1.cursor.firstBatch, rv2.cursor.firstBatch);

        // Fetch all documents whose 'a' array contains 4 => 3
        command.filter = { a: { $elemMatch: { $eq: 4 }}}
        rv1 = await mng.runCommand(command);
        rv2 = await mxs.runCommand(command);
        assert.equal(rv1.cursor.firstBatch.length, 3);
        assert.deepEqual(rv1.cursor.firstBatch, rv2.cursor.firstBatch);

        // Fetch all documents whose 'a' array contains 7 => 0
        command.filter = { a: { $elemMatch: { $eq: 7 }}}
        rv1 = await mng.runCommand(command);
        rv2 = await mxs.runCommand(command);
        assert.equal(rv1.cursor.firstBatch.length, 0);
        assert.deepEqual(rv1.cursor.firstBatch, rv2.cursor.firstBatch);
    });

    it('Supports nested $elemMatch', async function () {
        await drop(misc);

        var documents = [
            { _id: 1, a: [{b: 1}, {b: 2}, {b: 3}] },
            { _id: 2, a: [{b: 2}, {b: 3}, {b: 4}] },
            { _id: 3, a: [{b: 3}, {b: 4}, {b: 5}] },
            { _id: 4, a: [{b: 4}, {b: 5}, {b: 6}] },
        ];

        var command = {
            insert: misc,
            documents: documents
        };

        await mng.runCommand(command);
        await mxs.runCommand(command);

        command = {
            find: misc
        };

        // Fetch all documents whose 'a' array contains a document with b == 1 => 1
        command.filter = { a: { $elemMatch: { b: { $eq: 1} }}};
        var rv1 = await mng.runCommand(command);
        var rv2 = await mxs.runCommand(command);
        assert.equal(rv1.cursor.firstBatch.length, 1);
        assert.deepEqual(rv1.cursor.firstBatch, rv2.cursor.firstBatch);

        // Fetch all documents whose 'a' array contains a document with b == 4 => 3
        command.filter = { a: { $elemMatch: { b: { $eq: 4 }}}};
        rv1 = await mng.runCommand(command);
        rv2 = await mxs.runCommand(command);
        assert.equal(rv1.cursor.firstBatch.length, 3);
        assert.deepEqual(rv1.cursor.firstBatch, rv2.cursor.firstBatch);

        // Fetch all documents whose 'a' array contains a document with b == 7 => 0
        command.filter = { a: { $elemMatch: { b: { $eq: 7 }}}};
        rv1 = await mng.runCommand(command);
        rv2 = await mxs.runCommand(command);
        assert.equal(rv1.cursor.firstBatch.length, 0);
        assert.deepEqual(rv1.cursor.firstBatch, rv2.cursor.firstBatch);
    });

    it('Supports $size', async function () {
        drop(misc);

        var documents = [
            { _id: 1, f: [ 1, 2 ] },
            { _id: 2, f: [ 1, 2, 3 ] },
            { _id: 3, f: [ 1, 2 ] },
            { _id: 4, f: [ 1, 2, 3 ] },
            { _id: 5, f: [ 1, 2 ] }
        ];

        var command = {
            insert: misc,
            documents: documents
        };

        await mng.runCommand(command);
        await mxs.runCommand(command);

        command = {
            find: misc,
            filter: { f: { $size: 3 }}
        };

        var rv1 = await mng.runCommand(command);
        var rv2 = await mxs.runCommand(command);
        assert.equal(rv1.cursor.firstBatch.length, 2);
        assert.deepEqual(rv1.cursor.firstBatch, rv2.cursor.firstBatch);
    });

    it('Supports $all', async function () {
        drop(misc);

        var documents = [
            {
                _id: 1,
                code: "xyz",
                tags: [ "school", "book", "bag", "headphone", "appliance" ],
                qty: [
                    { size: "S", num: 10, color: "blue" },
                    { size: "M", num: 45, color: "blue" },
                    { size: "L", num: 100, color: "green" }
                ]
            },
            {
                _id: 2,
                code: "abc",
                tags: [ "appliance", "school", "book" ],
                qty: [
                    { size: "6", num: 100, color: "green" },
                    { size: "6", num: 50, color: "blue" },
                    { size: "8", num: 100, color: "brown" }
                ]
            },
            {
                _id: 3,
                code: "efg",
                tags: [ "school", "book" ],
                qty: [
                    { size: "S", num: 10, color: "blue" },
                    { size: "M", num: 100, color: "blue" },
                    { size: "L", num: 100, color: "green" }
                ]
            },
            {
                _id: 4,
                code: "ijk",
                tags: [ "electronics", "school" ],
                qty: [
                    { size: "M", num: 100, color: "green" }
                ]
            }
        ];

        var command = {
            insert: misc,
            documents: documents
        };

        await mng.runCommand(command);
        await mxs.runCommand(command);

        command = {
            find: misc,
            filter: { tags: { $all: [ "appliance", "school", "book" ] } }
        };

        var rv1 = await mng.runCommand(command);
        var rv2 = await mxs.runCommand(command);
        assert.equal(rv1.cursor.firstBatch.length, 2);
        assert.deepEqual(rv1.cursor.firstBatch, rv2.cursor.firstBatch);

    });

    it('Supports $type', async function () {
        drop(misc);

        var documents = [
            { _id: 1, f: true },
            { _id: 2, f: "hello" },
            { _id: 3, f: 4711 },
            { _id: 4, f: 3.14 },
            { _id: 5, f: {} },
            { _id: 6, f: [] },
        ];

        var command = {
            insert: misc,
            documents: documents
        };

        await mng.runCommand(command);
        await mxs.runCommand(command);

        var types = [
            { code: 1,  alias: "double", value: documents[3].f },
            { code: 2,  alias: "string", value: documents[1].f },
            { code: 3,  alias: "object", value: documents[4].f },
            { code: 4,  alias: "array",  value: documents[5].f },
            { code: 8,  alias: "bool",   value: documents[0].f },
            { code: 16, alias: "int",    value: documents[2].f },
        ];

        for (var type of types) {
            command = {
                find: misc,
                filter: { f: { $type: type.code }}
            };

            async function check() {
                var rv1 = await mng.runCommand(command);
                var rv2 = await mxs.runCommand(command);
                assert.equal(rv1.cursor.firstBatch.length, 1);
                assert.deepEqual(rv1.cursor.firstBatch[0].f, type.value);
                assert.deepEqual(rv1.cursor.firstBatch, rv2.cursor.firstBatch);
            }

            await check();

            command = {
                find: misc,
                filter: { f: { $type: [ type.code ] }}
            };

            await check();

            command = {
                find: misc,
                filter: { f: { $type: type.alias }}
            };

            await check();

            command = {
                find: misc,
                filter: { f: { $type: [ type.alias ] }}
            };

            await check();
        }

        command = {
            find: misc,
            filter: { f: { $type: "number" }}
        };

        var rv1 = await mng.runCommand(command);
        var rv2 = await mxs.runCommand(command);
        assert.equal(rv1.cursor.firstBatch.length, 2);
        assert.deepEqual(rv1.cursor.firstBatch, rv2.cursor.firstBatch);

        command = {
            find: misc,
            filter: { f: { $type: [] } }
        };

        async function check_n(command, n) {
            var rv1 = await mng.runCommand(command);
            var rv2 = await mxs.runCommand(command);
            assert.equal(rv1.cursor.firstBatch.length, n);
            assert.deepEqual(rv1.cursor.firstBatch, rv2.cursor.firstBatch);
        }

        for (var type of types) {
            command.filter.f.$type.push(type.code);
        }

        await check_n(command, types.length);

        command.filter.f.$type = [];

        for (var type of types) {
            command.filter.f.$type.push(type.alias);
        }

        await check_n(command, types.length);
    });

    after(function () {
        drop(misc);
        drop(name);

        mng.close();
        mxs.close();
    });
});
