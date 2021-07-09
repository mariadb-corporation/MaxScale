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

// https://docs.mongodb.com/manual/reference/command/resetError/

const assert = require('assert');
const test = require('./nosqltest')

const name = "resetError";

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

        await mng.reset(name);
        await mxs.reset(name);
    });

    it('Resets existing error.', async function () {
        await mng.ntRunCommand({count: 1}); // Wants a collection name.
        await mxs.ntRunCommand({count: 1});

        var rv1;
        var rv2;

        rv1 = await mng.getLastError();
        assert.notEqual(rv1.code, 0);

        rv2 = await mxs.getLastError();
        assert.notEqual(rv2.code, 0);

        mng.runCommand({resetError: 1});
        mxs.runCommand({resetError: 1});

        rv1 = await mng.getLastError();
        assert.equal(rv1.err, null);
        assert.equal(rv1.code, undefined);

        rv2 = await mxs.getLastError();
        assert.equal(rv2.err, null);
        assert.equal(rv2.code, undefined);

        delete rv1.connectionId;
        delete rv2.connectionId;
        assert.deepEqual(rv1, rv2);
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
