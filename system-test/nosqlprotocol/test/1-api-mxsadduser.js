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

const name = "mxsAddUser";

describe(name, function () {
    this.timeout(test.timeout);

    let nosql;
    let conn;

    var db = "mxsAddUser";

    async function remove_user(user) {
        await nosql.ntRunCommand({mxsRemoveUser: user.user});
    }

    async function add_user(user) {
        await remove_user(user);

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

        await nosql.ntRunCommand({mxsRemoveUser: "bob"});
    });

    it('Can add user 1.', async function () {
        var user = {
            user: "bob1",
            pwd: "bob1spwd",
            roles: [
                {db: "db", role: "userAdmin"},
                {db: "test1", role: "read"},
                {db: "test2", role: "readWrite"},
                {db: "test3", role: "dbOwner"}
            ],
            mechanisms: ["SCRAM-SHA-1", "SCRAM-SHA-256"]
        };

        await add_user(user);
        var added_user = await fetch_user(user);

        // Remove fields that won't be find on the added one.
        delete user.pwd;

        assert.deepEqual(user, added_user);
        await remove_user(user);
    });

    it('Can add user 2.', async function () {
        var user = {
            user: "bob2",
            pwd: "bob2spwd",
            roles: [
                {db: "db", role: "userAdmin"},
                {db: "test1", role: "read"},
                {db: "test2", role: "readWrite"},
                {db: "test3", role: "dbOwner"}
            ],
        };

        await add_user(user);
        var added_user = await fetch_user(user);

        // Remove fields that won't be find on the added one.
        delete user.pwd;

        // If mechanisms is missing in creation, then all will be added.
        user.mechanisms = ["SCRAM-SHA-1", "SCRAM-SHA-256"];

        assert.deepEqual(user, added_user);
        await remove_user(user);
    });

    it('Can add user 3.', async function () {
        var user = {
            user: "bob3",
            pwd: "bob3spwd",
            roles: [ "userAdmin" ],
            mechanisms: ["SCRAM-SHA-1", "SCRAM-SHA-256"]
        };

        await add_user(user);
        var added_user = await fetch_user(user);

        // Remove fields that won't be find on the added one.
        delete user.pwd;

       // If a role is added as a string (and not document), it role will be
       // added to the database on which the user is created.
       user.roles[0] = { db: db, role: "userAdmin" };

       assert.deepEqual(user, added_user);
       await remove_user(user);
    });

    it('Cannot add without pwd.', async function () {
        var user = {
            user: "bob4",
            roles: [ "userAdmin" ],
            mechanisms: ["SCRAM-SHA-1", "SCRAM-SHA-256"]
        };

        try {
            await add_user(user);
            assert.fail("Creation did not fail.");
        }
        catch (x) {
        }
    });

    it('Cannot add without roles.', async function () {
        var user = {
            user: "bob5",
            pwd: "bob5spwd",
            mechanisms: ["SCRAM-SHA-1", "SCRAM-SHA-256"]
        };

        try {
            await add_user(user);
            assert.fail("Creation did not fail.");
        }
        catch (x) {
        }
    });

    it('Cannot add with unknown mechanism.', async function () {
        var user = {
            user: "bob6",
            pwd: "bob6spwd",
            mechanisms: ["SCRAM-SHA-1", "SCRAM-SHA-256", "XYZ"]
        };

        try {
            await add_user(user);
            assert.fail("Creation did not fail.");
        }
        catch (x) {
        }
    });

    it('Cannot add with unknown role.', async function () {
        var user = {
            user: "bob7",
            pwd: "bob7spwd",
            mechanisms: ["SCRAM-SHA-1", "SCRAM-SHA-256"],
            roles: ["XYZ"]
        };

        try {
            await add_user(user);
            assert.fail("Creation did not fail.");
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
