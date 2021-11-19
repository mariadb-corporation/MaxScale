/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// https://docs.mongodb.com/manual/reference/command/ping

const assert = require('assert');
const test = require('./nosqltest')

const name = "ping";

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

    it('Command implemented.', async function () {
        var rv1 = await mxs.runCommand({ping: 1});
        var rv2 = await mxs.runCommand({ping: 1});

        assert.deepEqual(rv1, rv2);
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
