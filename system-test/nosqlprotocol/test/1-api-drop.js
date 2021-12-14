/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// https://docs.mongodb.com/manual/reference/command/drop/

const assert = require('assert');
const test = require('./nosqltest')

const name = "drop";

describe(name, function () {
    this.timeout(test.timeout);

    let mng;
    let mxs;

    /*
     * MOCHA
     */
    before(async function () {
        mng = await test.MDB.create(test.MngMongo);
        mxs = await test.MDB.create(test.MxsMongo);
    });

    it('Cannot drop non-existent collection.', async function () {
        await mng.reset(name);
        await mxs.reset(name);

        var rv1 = await mng.ntRunCommand({drop: name});
        var rv2 = await mxs.ntRunCommand({drop: name});

        assert.equal(rv1.code, 26); // Namespace not found.

        assert.deepEqual(rv1, rv2);
    });

    it('Can drop existent collection.', async function () {
        await mng.reset(name);
        await mxs.reset(name);

        var rv1;
        var rv2;

        rv1 = await mng.runCommand({create: name});
        rv2 = await mxs.runCommand({create: name});
        assert.equal(rv1.ok, 1);
        assert.equal(rv2.ok, 1);

        var n = 13;

        rv1 = await mng.insert_n(name, n);
        rv2 = await mxs.insert_n(name, n);
        assert.equal(rv1.ok, 1);
        assert.equal(rv2.ok, 1);

        rv1 = await mng.find(name);
        rv2 = await mxs.find(name);
        assert.equal(rv1.cursor.firstBatch.length, n);
        assert.equal(rv2.cursor.firstBatch.length, n);

        rv1 = await mng.ntRunCommand({drop: name});
        rv2 = await mxs.ntRunCommand({drop: name});
        assert.equal(rv1.ok, 1);
        assert.equal(rv2.ok, 1);

        rv1 = await mng.find(name);
        rv2 = await mxs.find(name);
        assert.equal(rv1.cursor.firstBatch.length, 0);
        assert.equal(rv2.cursor.firstBatch.length, 0);
    });

    after(async function () {
        if (mxs) {
            await mxs.close();
        }

        if (mng) {
            await mng.close();
        }
    });
});
