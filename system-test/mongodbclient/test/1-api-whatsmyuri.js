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

// https://docs.mongodb.com/manual/reference/command/whatsmyuri

const assert = require('assert');
const test = require('./mongotest')

const name = "whatsmyuri";

describe(name, function () {
    let mng;
    let mxs;

    function check_fields(doc) {
        assert.notEqual(doc.you, undefined);
        assert.notEqual(doc.ok, undefined);
    }

    /*
     * MOCHA
     */
    before(async function () {
        mng = await test.MDB.create(test.MngMongo);
        mxs = await test.MDB.create(test.MxsMongo);
    });

    it('Command implemented.', async function () {
        var rv1 = await mxs.runCommand({whatsmyuri: 1});
        var rv2 = await mxs.runCommand({whatsmyuri: 1});

        check_fields(rv1);
        check_fields(rv2);
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
