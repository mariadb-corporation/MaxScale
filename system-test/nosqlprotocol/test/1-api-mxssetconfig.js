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

const assert = require('assert');
const test = require('./nosqltest')
const error = test.error;

const name = "mxsSetConfig";

describe(name, function () {
    this.timeout(test.timeout);

    let mxs;

    /*
     * MOCHA
     */
    before(async function () {
        mxs = await test.MDB.create(test.MxsMongo);
    });

    it('Cannot use with non-admin database.', async function () {
        var rv = await mxs.ntRunCommand({mxsSetConfig: {}});

        assert.equal(rv.code, error.UNAUTHORIZED);
    });

    it('Can use with admin database.', async function () {
        var c = {};
        await mxs.adminCommand({mxsSetConfig: c});

        // Valid values
        c.on_unknown_command = 'return_empty';
        await mxs.adminCommand({mxsSetConfig: c});
        c.on_unknown_command = 'return_error';
        await mxs.adminCommand({mxsSetConfig: c});

        c.auto_create_databases = true;
        await mxs.adminCommand({mxsSetConfig: c});

        c.auto_create_tables = true;
        await mxs.adminCommand({mxsSetConfig: c});

        c.id_length = 80;
        await mxs.adminCommand({mxsSetConfig: c});

        c.ordered_insert_behavior = "atomic";
        await mxs.adminCommand({mxsSetConfig: c});
        c.ordered_insert_behavior = "default";
        await mxs.adminCommand({mxsSetConfig: c});

        var rv;
        // Invalid values
        c = { on_unknown_command: 'blah' };
        rv = await mxs.ntAdminCommand({mxsSetConfig: c});
        assert.equal(rv.code, error.BAD_VALUE);

        c = { auto_create_databases: 'blah' };
        rv = await mxs.ntAdminCommand({mxsSetConfig: c});
        assert.equal(rv.code, error.TYPE_MISMATCH);

        c = { auto_create_tables: 'blah' };
        rv = await mxs.ntAdminCommand({mxsSetConfig: c});
        assert.equal(rv.code, error.TYPE_MISMATCH);

        c = { id_length: 0 };
        rv = await mxs.ntAdminCommand({mxsSetConfig: c});
        assert.equal(rv.code, error.BAD_VALUE);

        c = { ordered_insert_behavior: 'blah' };
        rv = await mxs.ntAdminCommand({mxsSetConfig: c});
        assert.equal(rv.code, error.BAD_VALUE);

        // Invalid key
        c = { no_such_key: 1 };
        rv = await mxs.ntAdminCommand({mxsSetConfig: c});
        assert.equal(rv.code, error.NO_SUCH_KEY);
    });

    after(async function () {
        if (mxs) {
            await mxs.close();
        }
    });
});
