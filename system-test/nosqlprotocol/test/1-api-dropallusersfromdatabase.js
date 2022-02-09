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

const name = "dropAllUsersFromDatabase";

describe(name, function () {
    this.timeout(test.timeout);

    let nosql1;
    let nosql2;
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

    async function can_create_user(nosql, user_name, db) {
        var user = {
            user: user_name,
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

        var rv = await mariadb.query("SELECT user FROM mysql.user WHERE user = '" + db + user_name + "'");

        assert.equal(rv.length, 1);
    }

    async function create_n_users(nosql, db, n) {
        for (i = 0; i < n; ++i) {
            var name = "nosqlbob-" + i;

            await can_create_user(nosql, name, db);
        }

        var rv = await mariadb.query("SELECT user FROM mysql.user WHERE user LIKE '" + db + "nosqlbob-%'");

        assert.equal(rv.length, n);
    }

    async function can_drop_all_users(nosql, db, n) {
        await create_n_users(nosql, db, n);

        await nosql.runCommand({ dropAllUsersFromDatabase: 1 });

        var rv = await mariadb.query("SELECT user FROM mysql.user WHERE user LIKE '" + db + "nosqlbob-%'");

        assert.equal(rv.length, 0);
    }

    /*
     * MOCHA
     */
    before(async function () {
        nosql1 = await test.NoSQL.create("nosql");
        nosql2 = await test.NoSQL.create("mariadb");
        mariadb = await test.MariaDB.createConnection();
    });

    it('Can drop all users.', async function () {
        await can_drop_all_users(nosql1, "nosql.", 10);
    });

    it('Can drop all "mariadb" users.', async function () {
        await can_drop_all_users(nosql2, "", 10);
    });

    after(async function () {
        if (nosql2) {
            await nosql2.close();
        }

        if (nosql1) {
            await nosql1.close();
        }

        if (mariadb) {
            await mariadb.end();
        }
    });
});
