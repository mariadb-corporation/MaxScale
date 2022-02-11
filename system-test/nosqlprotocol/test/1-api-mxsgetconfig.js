/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
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

    let nosql;

    /*
     * MOCHA
     */
    before(async function () {
        nosql = await test.NoSQL.create();
    });

    it('Cannot use with non-admin database.', async function () {
        var rv = await nosql.ntRunCommand({mxsGetConfig: 1});

        assert.equal(rv.code, error.UNAUTHORIZED);
    });

    it('Can use with admin database.', async function () {
        var rv = await nosql.adminCommand({mxsGetConfig: 1});

        assert.equal(rv.ok, 1);
        assert.notEqual(rv.config, undefined);
        var c = rv.config;
        assert.notEqual(c.auto_create_databases, undefined);
        assert.notEqual(c.auto_create_tables, undefined);
        assert.notEqual(c.cursor_timeout, undefined);
        assert.notEqual(c.debug, undefined);
        assert.notEqual(c.log_unknown_command, undefined);
        assert.notEqual(c.on_unknown_command, undefined);
        assert.notEqual(c.ordered_insert_behavior, undefined);

        delete c.auto_create_databases;
        delete c.auto_create_tables;
        delete c.cursor_timeout;
        delete c.debug;
        delete c.log_unknown_command;
        delete c.on_unknown_command;
        delete c.ordered_insert_behavior;

        assert.equal(Object.keys(c).length, 0);
    });

    after(async function () {
        if (nosql) {
            await nosql.close();
        }
    });
});
