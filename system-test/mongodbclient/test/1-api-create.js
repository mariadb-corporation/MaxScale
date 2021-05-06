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

// https://docs.mongodb.com/manual/reference/command/create/

const assert = require('assert');
const test = require('./mongotest')

const name = "create";

describe(name, function () {
    let mng;
    let mxs;

    /*
     * MOCHA
     */
    before(async function () {
        mng = await test.MDB.create(test.MngMongo);
        mxs = await test.MDB.create(test.MxsMongo);
    });

    it('Can create new collection.', async function () {
        await mng.reset(name);
        await mxs.reset(name);

        var rv1 = await mng.runCommand({create: name});
        var rv2 = await mxs.runCommand({create: name});

        assert.equal(rv1.ok, 1);
        assert.equal(rv2.ok, 1);

        assert.deepEqual(rv1, rv2);
    });

    it('Cannot create duplicate collection.', async function () {
        var rv1 = await mng.ntRunCommand({create: name});
        var rv2 = await mxs.ntRunCommand({create: name});

        assert.equal(rv1.ok, 0);
        assert.equal(rv2.ok, 0);

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
