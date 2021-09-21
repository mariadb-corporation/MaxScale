/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// https://docs.mongodb.com/manual/reference/command/update/

const assert = require('assert');
const test = require('./nosqltest');
const error = test.error;
const mongodb = test.mongodb;

const name = "update";

describe(name, function () {
    this.timeout(test.timeout);

    let mng;
    let mxs;

    /*
     * HELPERS
     */
    async function drop()
    {
        var command = {
            drop: name
        };

        var rv1 = await mng.ntRunCommand(command);
        var rv2 = await mxs.ntRunCommand(command);
    }

    async function deleteAll()
    {
        await mng.deleteAll(name);
        await mxs.deleteAll(name);
    }

    /*
     * MOCHA
     */
    before(async function () {
        mxs = await test.MDB.create(test.MxsMongo);
        mng = await test.MDB.create(test.MngMongo);
    });

    it('Can update in non-existing collection/table.', async function () {
        await drop();

        var command = {
            update: name,
            updates: [{q:{},u:{}}]
        };

        var rv1 = await mng.runCommand(command);
        var rv2 = await mxs.runCommand(command);

        assert.deepEqual(rv1, rv2);
    });

    it('A replacement update does not nuke the id.', async function () {
        await drop();

        var doc = {
            _id: "hello",
            a: 1
        };

        await mng.runCommand({insert: name, documents: [doc]});
        await mxs.runCommand({insert: name, documents: [doc]});

        var rv1 = mng.runCommand({find: name});
        var rv2 = mxs.runCommand({find: name});

        assert.deepEqual(rv1, rv2);

        rv1 = await mng.runCommand({update:name, updates: [{q:{}, u: {b:2}}]});
        rv2 = await mxs.runCommand({update:name, updates: [{q:{}, u: {b:2}}]});

        assert.deepEqual(rv1, rv2);

        var rv1 = await mng.runCommand({find: name});
        var rv2 = await mxs.runCommand({find: name});

        assert.equal(rv1.cursor.firstBatch[0]._id, "hello");
        assert.deepEqual(rv1, rv2);
    });

    it('A replacement update does not overwrite the id.', async function () {
        // MongoDB considers it an error if you try to change the '_id' with
        // a replacement document. To detect that we would have to check whether
        // a replacement document has the '_id' field (trivial) but the error
        // must be generated only at clientReply() time, so that 'ordered' works
        // as expected. That would be somewhat messy and as it surely must be
        // an edge-case to rely upon that to cause an error, we will just ignore
        // an '_id' field.

        await drop();

        var from = {
            _id: "hello",
            a: 1
        };

        await mxs.runCommand({insert: name, documents: [from]});

        var rv = await mxs.runCommand({find: name});

        assert.equal(rv.cursor.firstBatch.length, 1);

        var to = rv.cursor.firstBatch[0];

        assert.deepEqual(from, to);

        // Try to change the id.
        await mxs.runCommand({update:name, updates: [{q:{}, u: {_id: "world", a: 2}}]});

        var rv = await mxs.runCommand({find: name});

        assert.equal(rv.cursor.firstBatch.length, 1);

        var to = rv.cursor.firstBatch[0];

        assert.equal(to.a, 2);
        assert.equal(to._id, from._id);
    });

    it('Handles $set.', async function () {
        await deleteAll();

        var original = {
            a: 1,
            b: 2,
            c: 3,
            nested: {
                d: 4
            },
            _id: mongodb.ObjectId()
        };

        var command = {insert: name, documents: [original]};
        await mng.runCommand(command);
        await mxs.runCommand(command);

        // Single value.
        command = {update: name, updates: [{q: {}, u: { $set: { a: 11 }}}]};
        await mng.runCommand(command);
        await mxs.runCommand(command);

        var rv1 = await mng.runCommand({find: name});
        var rv2 = await mxs.runCommand({find: name});

        assert.deepEqual(rv1.cursor.firstBatch, rv2.cursor.firstBatch);

        // Multiple values.
        command = {update: name, updates: [{q: {}, u: { $set: { b: 21, c: 22 }}}]};
        await mng.runCommand(command);
        await mxs.runCommand(command);

        var rv1 = await mng.runCommand({find: name});
        var rv2 = await mxs.runCommand({find: name});

        assert.deepEqual(rv1.cursor.firstBatch, rv2.cursor.firstBatch);

        // Nested value
        command = {update: name, updates: [{q: {}, u: { $set: { "nested.d": 34 }}}]};
        await mng.runCommand(command);
        await mxs.runCommand(command);

        var rv1 = await mng.runCommand({find: name});
        var rv2 = await mxs.runCommand({find: name});

        assert.deepEqual(rv1.cursor.firstBatch, rv2.cursor.firstBatch);
    });

    it('Handles $unset.', async function () {
        await deleteAll();

        var original = {
            a: 1,
            b: 2,
            c: 3,
            nested: {
                d: 4
            },
            _id: mongodb.ObjectId()
        };

        var command = {insert: name, documents: [original]};
        await mng.runCommand(command);
        await mxs.runCommand(command);

        // Single value.
        command = {update: name, updates: [{q: {}, u: { $unset: { a: "" }}}]};
        await mng.runCommand(command);
        await mxs.runCommand(command);

        var rv1 = await mng.runCommand({find: name});
        var rv2 = await mxs.runCommand({find: name});

        assert.deepEqual(rv1.cursor.firstBatch, rv2.cursor.firstBatch);

        // Multiple values.
        command = {update: name, updates: [{q: {}, u: { $unset: { b: "", c: "" }}}]};
        await mng.runCommand(command);
        await mxs.runCommand(command);

        var rv1 = await mng.runCommand({find: name});
        var rv2 = await mxs.runCommand({find: name});

        assert.deepEqual(rv1.cursor.firstBatch, rv2.cursor.firstBatch);

        // Nested value
        command = {update: name, updates: [{q: {}, u: { $unset: { "nested.d": "" }}}]};
        await mng.runCommand(command);
        await mxs.runCommand(command);

        var rv1 = await mng.runCommand({find: name});
        var rv2 = await mxs.runCommand({find: name});

        assert.deepEqual(rv1.cursor.firstBatch, rv2.cursor.firstBatch);
    });

    it('Errors on unsupported operator.', async function () {
        var command = {update: name, updates: [{q: {}, u: { $inc: { a: 1 }}}]};
        var rv = await mxs.ntRunCommand(command);

        assert.equal(rv.code, error.COMMAND_FAILED);
    });

    it('Can update multiple documents.', async function () {
        await deleteAll();

        var originals = [
            {
                a: 1,
                b: 2,
                _id: mongodb.ObjectId()
            },
            {
                a: 1,
                b: 3,
                _id: mongodb.ObjectId()
            }
        ];

        var command = {insert: name, documents: originals};
        await mng.runCommand(command);
        await mxs.runCommand(command);

        var command = {update: name, updates: [{q: {}, u: { "$set": { b: 4 }}, multi: true }]};
        var rv1 = await mng.runCommand(command);
        var rv2 = await mxs.runCommand(command);

        var rv1 = await mng.runCommand({find: name});
        var rv2 = await mxs.runCommand({find: name});

        assert.equal(rv1.cursor.firstBatch[0].b, 4);
        assert.equal(rv1.cursor.firstBatch[1].b, 4);
        assert.deepEqual(rv1.cursor.firstBatch, rv2.cursor.firstBatch);
    });

    it('Can update with query.', async function () {
        await deleteAll();

        var originals = [
            {
                a: 1,
                b: 1,
                _id: mongodb.ObjectId()
            },
            {
                a: 2,
                b: 1,
                _id: mongodb.ObjectId()
            },
            {
                a: 3,
                b: 1,
                _id: mongodb.ObjectId()
            }
        ];

        var command = {insert: name, documents: originals};
        await mng.runCommand(command);
        await mxs.runCommand(command);

        var command = {update: name, updates: [{q: {a: 2}, u: { b: 4 }}]};
        await mng.runCommand(command);
        await mxs.runCommand(command);

        var rv1 = await mng.runCommand({find: name});
        var rv2 = await mxs.runCommand({find: name});

        assert.notEqual(rv1.cursor.firstBatch[0].b, 4);
        assert.equal(rv1.cursor.firstBatch[1].b, 4);
        assert.notEqual(rv1.cursor.firstBatch[2].b, 4);
        assert.deepEqual(rv1.cursor.firstBatch, rv2.cursor.firstBatch);
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
