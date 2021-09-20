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

// https://docs.mongodb.com/manual/reference/command/getLastError/

const test = require('./nosqltest');

const config = test.config;

const mariadb = test.mariadb;
const mongodb = test.mongodb;
const assert = test.assert;

describe('getLastError', function () {
    this.timeout(test.timeout);

    let mng;
    let mxs;
    let mngdb;
    let mxsdb;

    const name = "getLastError";

    /*
     * HELPERS
     */
    async function reset_db(db) {
        try {
            await db.command({ drop: name});
        }
        catch (x)
        {
            if (x.code != 26) // NameSpace not found
            {
                throw x;
            }
        }
    }

    async function reset() {
        await reset_db(mngdb);
        await reset_db(mxsdb);
    }

    async function get_last_error_db(db) {
        var command = {"getLastError": 1 };

        var rv = await db.command(command);

        return rv;
    }

    async function command(command)
    {
        try {
            await mngdb.command(command);
        }
        catch (x) {
        }

        try {
            await mxsdb.command(command);
        }
        catch (x) {
        }
    }

    /*
     * MOCHA
     */
    before(async function () {
        mxs = await test.MxsMongo.createClient();
        mng = await test.MngMongo.createClient();

        mxsdb = mxs.db("test");
        mngdb = mng.db("test");

        await reset();
    });

    it('Returns default error after reset.', async function () {
        await command({resetError:1});

        var mngrv = await get_last_error_db(mngdb);
        var mxsrv = await get_last_error_db(mxsdb);

        // Won't ever be the same.
        delete mngrv.connectionId;
        delete mxsrv.connectionId;

        assert.deepEqual(mngrv, mxsrv);
    });

    it('Returns error after wrong type of field.', async function () {
        await command({count:1}); // Wants a collection name.

        // The nodejs library will turn a returned error into an exception
        // also when the error is fetched using getLastError(). Further, the
        // type does not reliably seem what it should be, so we are happy
        // if getLastError() returns an error when it should.

        try {
            await get_last_error(mngdb);
            assert.ok(false);
        }
        catch (x) {
        }

        try {
            await get_last_error(mxsdb);
            assert.ok(false);
        }
        catch (x) {
        }
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
