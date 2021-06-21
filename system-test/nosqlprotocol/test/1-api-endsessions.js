/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-06-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// https://docs.mongodb.com/manual/reference/command/endSessions/

const assert = require('assert');
const test = require('./nosqltest')

const name = "endSessions";

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

    it('Responds to endSessions.', async function () {
        var rv1 = await mng.runCommand({endSessions: []});
        var rv2 = await mxs.runCommand({endSessions: []});

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
