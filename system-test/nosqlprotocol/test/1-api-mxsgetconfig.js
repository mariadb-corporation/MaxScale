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

const assert = require('assert');
const test = require('./nosqltest')
const error = test.error;

const name = "mxsGetConfig";

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
        var rv = await mxs.ntRunCommand({mxsGetConfig: 1});

        assert.equal(rv.code, error.UNAUTHORIZED);
    });

    it('Can use with admin database.', async function () {
        var rv = await mxs.adminCommand({mxsGetConfig: 1});

        assert.equal(rv.ok, 1);
        assert.notEqual(rv.config, undefined);
        var c = rv.config;
        assert.notEqual(c.on_unknown_command, undefined);
        assert.notEqual(c.auto_create_databases, undefined);
        assert.notEqual(c.auto_create_tables, undefined);
        assert.notEqual(c.id_length, undefined);
        assert.notEqual(c.insert_behavior, undefined);

        delete c.on_unknown_command;
        delete c.auto_create_databases;
        delete c.auto_create_tables;
        delete c.id_length;
        delete c.insert_behavior;

        assert.equal(Object.keys(c).length, 0);
    });

    after(async function () {
        if (mxs) {
            await mxs.close();
        }
    });
});
