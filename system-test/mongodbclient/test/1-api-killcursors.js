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

// https://docs.mongodb.com/manual/reference/command/dropDatabase

const assert = require('assert');
const test = require('./mongotest')

const name = "killCursors";

describe(name, function () {
    let mxs;
    let mng;

    /*
     * MOCHA
     */
    before(async function () {
        mng = await test.MDB.create(test.MngMongo);
        mxs = await test.MDB.create(test.MxsMongo);

        await mng.reset(name);
        await mng.reset(name);
    });

    it('Cant kill when there are no cursors to kill.', async function () {
        var rv1 = await mng.nothrowCommand({"killCursors": name, cursors: []});
        var rv2 = await mxs.nothrowCommand({"killCursors": name, cursors: []});

        assert.equal(rv1.code, 2); // Bad value
        assert.equal(rv2.code, 2); // Bad value
    });

    it('Can kill an existing cursor.', async function () {
        await mng.insert_n(name, 50);
        await mxs.insert_n(name, 50);

        var rv1;
        var rv2;

        // Do a find, but request just 10 out of the 50 that were created.
        rv1 = await mng.find(name, { batchSize: 10 });
        rv2 = await mxs.find(name, { batchSize: 10 });

        assert.equal(rv1.cursor.firstBatch.length, 10);
        assert.equal(rv2.cursor.firstBatch.length, rv1.cursor.firstBatch.length);

        // Get next batch using the cursor, but again only 10, so the cursor is still alive.
        rv1 = await mng.runCommand({getMore: rv1.cursor.id, collection: name, batchSize: 10 });
        rv2 = await mxs.runCommand({getMore: rv2.cursor.id, collection: name, batchSize: 10 });

        assert.equal(rv1.cursor.nextBatch.length, 10);
        assert.equal(rv2.cursor.nextBatch.length, rv1.cursor.nextBatch.length);

        var id1 = rv1.cursor.id;
        var id2 = rv2.cursor.id;

        // Kill the cursors, should succeed.
        rv1 = await mng.runCommand({"killCursors": name, cursors: [ id1 ]});
        rv2 = await mxs.runCommand({"killCursors": name, cursors: [ id2 ]});

        assert.equal(rv1.cursorsKilled.length, 1);
        assert.equal(rv2.cursorsKilled.length, 1);

        // Kill again, the cursors should not be found.
        rv1 = await mng.runCommand({"killCursors": name, cursors: [ id1 ]});
        rv2 = await mxs.runCommand({"killCursors": name, cursors: [ id2 ]});

        assert.equal(rv1.cursorsNotFound.length, 1);
        assert.equal(rv2.cursorsNotFound.length, 1);

        // Try to get another batch, should fail as the cursors were killed.
        rv1 = await mng.nothrowCommand({getMore: id1, collection: name, batchSize: 10 });
        rv2 = await mxs.nothrowCommand({getMore: id2, collection: name, batchSize: 10 });

        assert.equal(rv1.code, 43); // Cursor not found
        assert.equal(rv2.code, 43); // Cursor not found
    });

    after(async function () {
        if (mng) {
            await mng.close();
        }

        if (mxs) {
            await mxs.close();
        }
    });
});
