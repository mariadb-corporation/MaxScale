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

    async function fetch_user(user_name, nosql) {
        var rv = await nosql.runCommand({usersInfo: user_name});
        var users = rv.users;

        return fetched_user = users[0];
    }

    async function create_user(user_name, nosql) {
        await drop_user(user_name, nosql);

        var command = {
            createUser: "bob",
            pwd: "pwd",
            roles: [
                {db: "nosql", role: "userAdmin"},
                {db: "nosql", role: "readWrite"},
            ],
            mechanisms: ["SCRAM-SHA-1", "SCRAM-SHA-256"]
        };

        await nosql.runCommand(command);
    }

    async function mariadb_user_exists(nosql) {
        var db = nosql.dbName();

        if (db == "mariadb") {
            db = "";
        }
        else {
            db = db + ".";
        }

        var rv = await mariadb.query("SELECT user FROM mysql.user WHERE user = '" + db + "bob'");

        return rv.length != 0;
    }

    /*
     * MOCHA
     */
    before(async function () {
        nosql = await test.NoSQL.create("nosql");
        mariadb = await test.MariaDB.createConnection();
    });

    it('Can drop existing user.', async function () {
        await create_user("bob", nosql);

        var user = await fetch_user("bob", nosql);
        assert.notEqual(user, undefined);
        assert.ok(await mariadb_user_exists(nosql), "MariaDB user not created.");

        await nosql.runCommand({dropUser: "bob"});

        var user = await fetch_user("bob", nosql);
        assert.equal(user, undefined);

        assert.ok(!await mariadb_user_exists(nosql), "MariaDB user not dropped.");
    });

    it('Cannot drop non-existing user.', async function () {
        try {
            await nosql.runCommand({dropUser: "bob"});
            assert.fail("Dropping non-existing used succeeded.");
        }
        catch (x) {
        }
    });

    it('Can drop existing user in "mariadb".', async function () {
        nosql2 = await test.NoSQL.create("mariadb");
        await create_user("bob", nosql2);

        try {
            var user = await fetch_user("bob", nosql2);
            assert.notEqual(user, undefined);
            assert.ok(await mariadb_user_exists(nosql2), "MariaDB user not created.");

            await nosql2.runCommand({dropUser: "bob"});

            var user = await fetch_user("bob", nosql2);
            assert.equal(user, undefined);

            assert.ok(!await mariadb_user_exists(nosql2), "MariaDB user not dropped.");
        }
        finally {
            nosql2.close();
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
