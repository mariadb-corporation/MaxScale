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

// https://docs.mongodb.com/manual/reference/command/listCommands

const assert = require('assert');
const test = require('./mongotest')

const name = "listCommands";

describe(name, function () {
    this.timeout(test.timeout);

    let mxs;

    function check_standard_fields(doc) {
        assert.notEqual(doc.help, undefined);
        assert.notEqual(doc.adminOnly, undefined);
    }

    /*
     * MOCHA
     */
    before(async function () {
        mxs = await test.MDB.create(test.MxsMongo);
    });

    it('Command implemented.', async function () {
        var rv = await mxs.runCommand({listCommands: 1});

        var commands = rv.commands;

        for (command in commands) {
            check_standard_fields(commands[command]);
        }
    });

    after(async function () {
        if (mxs) {
            await mxs.close();
        }
    });
});
