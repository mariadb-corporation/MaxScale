/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

const assert = require('assert');
const test = require('./nosqltest')
const error = test.error;

const name = "mxsRemoveUser";

describe(name, function () {
    this.timeout(test.timeout);

    let nosql;
    let conn;

    var db = name;

    async function add_user(user) {
        await nosql.runCommand({mxsAddUser: user.user,
                                pwd: user.pwd,
                                mechanisms: user.mechanisms,
                                roles: user.roles});
    }

    async function fetch_user(user) {
        var rv = await nosql.runCommand({usersInfo: user.user});
        var users = rv.users;
        var added_user = users[0];

        assert.equal(added_user.db, db);
        assert.equal(added_user._id, db + "." + user.user);
        assert.notEqual(added_user.userId, undefined);

        delete added_user._id;
        delete added_user.db;
        delete added_user.userId;

        return added_user;
    }

    /*
     * MOCHA
     */
    before(async function () {
        nosql = await test.NoSQL.create(db);
    });

    it('Can remove existing user.', async function () {
        var user = { user: "bob", pwd: "bobspwd", roles: [] };

        await nosql.ntRunCommand({mxsRemoveUser: user.user});

        await add_user(user);
        await fetch_user(user); // Yes, it's there.

        await nosql.runCommand({mxsRemoveUser: user.user});

        try {
            await fetch_user(user);
            assert.fail("The user was found.");
        }
        catch (x) {
        }
    });

    it('Cannot remove nonexisting user.', async function () {
        var user = { user: "bob", pwd: "bobspwd", roles: [] };

        await nosql.ntRunCommand({mxsRemoveUser: user.user});

        try {
            await nosql.runCommand({mxsRemoveUser: user.user});
            assert.fail("Non-existent user could be removed.");
        }
        catch (x) {
        }
    });

    after(async function () {
        if (nosql) {
            await nosql.close();
        }

        if (conn) {
            await conn.end();
        }
    });
});
