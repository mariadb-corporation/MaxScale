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

const name = "createUser";

describe(name, function () {
    this.timeout(test.timeout);

    let nosql;
    let mariadb;

    async function drop_user(user_name, nosql) {
        dbName = nosql.dbName();

        if (dbName == "mariadb") {
            dbName = "";
        }
        else {
            dbName = dbName + ".";
        }

        // By doing it like this, we can be certain there are no traces left.
        // Either one is not sufficient, if the user exists in MariaDB but
        // not in nosqlprotocol, or vice versa.
        await mariadb.query("DROP USER IF EXISTS '" + dbName + user_name + "'@'%'");
        await nosql.ntRunCommand({mxsRemoveUser: user_name});
    }

    async function create_user(user, nosql) {
        await drop_user(user.user, nosql);
        await nosql.runCommand({createUser: user.user,
                                pwd: user.pwd,
                                mechanisms: user.mechanisms,
                                roles: user.roles});
    }

    async function fetch_user(user, nosql) {
        var db = nosql.dbName();

        var rv = await nosql.runCommand({usersInfo: user.user});
        var users = rv.users;
        var fetched_user = users[0];

        assert.equal(fetched_user.db, db);
        assert.equal(fetched_user._id, db + "." + user.user);
        assert.notEqual(fetched_user.userId, undefined);

        delete fetched_user._id;
        delete fetched_user.db;
        delete fetched_user.userId;

        return fetched_user;
    }

    async function can_create_user(nosql, db) {
        var user = {
            user: "bob",
            pwd: "bobspwd",
            roles: [
                {db: "nosql", role: "userAdmin"},
                {db: "nosql", role: "readWrite"},
            ],
            mechanisms: ["SCRAM-SHA-1", "SCRAM-SHA-256"]
        };

        await create_user(user, nosql);
        var fetched_user = await fetch_user(user, nosql);

        // Remove fields that won't be find on the fetched one.
        delete user.pwd;

        assert.deepEqual(user, fetched_user);

        var rv = await mariadb.query("SELECT user FROM mysql.user WHERE user = '" + db + "bob'");

        assert.equal(rv.length, 1);
    }

    /*
     * MOCHA
     */
    before(async function () {
        nosql = await test.NoSQL.create("nosql");
        mariadb = await test.MariaDB.createConnection();
    });

    it('Can create user.', async function () {
        await can_create_user(nosql, "nosql.");
    });

    it('Cannot create duplicate in same database.', async function () {
        try {
            can_create_user(nosql, "nosql.");
            assert.fail("Could create duplicate.");
        }
        catch (x) {
        }
    });

    it('Can create duplicate in different database.', async function () {
        var nosql2 = await test.NoSQL.create("nosql2");

        try {
            await can_create_user(nosql2, "nosql2.");
        }
        catch (x) {
            assert.fail("Could not create duplicate in different database.");
        }
        finally {
            await drop_user("bob", nosql2);
            nosql2.close();
        }
    });

    it('Can create user in mariadb.', async function () {
        var nosql3 = await test.NoSQL.create("mariadb");

        try {
            await can_create_user(nosql3, "");
        }
        catch (x) {
            assert.fail("Could not create user in 'mariadb' database.");
        }
        finally {
            await drop_user("bob", nosql3);
            nosql3.close();
        }
    });

    after(async function () {
        if (nosql) {
            await drop_user("bob", nosql);
            await nosql.close();
        }

        if (mariadb) {
            await mariadb.end();
        }
    });
});
