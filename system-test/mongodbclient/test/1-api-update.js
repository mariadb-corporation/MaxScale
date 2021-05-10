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

// https://docs.mongodb.com/manual/reference/command/insert/

const assert = require('assert');
const test = require('./mongotest');
const error = test.error;
const mongodb = test.mongodb;

const name = "update";

describe(name, function () {
    this.timeout(100000);
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
        console.log("MNG: ", rv1);
        var rv2 = await mxs.ntRunCommand(command);
        console.log("MXS: ", rv1);
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
        drop();

        var command = {
            update: name,
            updates: [{q:{},u:{}}]
        };

        var rv1 = await mng.runCommand(command);
        var rv2 = await mxs.runCommand(command);

        assert.deepEqual(rv1, rv2);
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
