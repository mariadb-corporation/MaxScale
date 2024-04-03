/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
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

    async function smoke_test_user(user, pwd, dbName) {
        var host = test.config.host;
        var port = test.config.nosql_port;
        var uri = "mongodb://" + user + ":" + pwd + "@" + host + ":" + port + "/" + dbName;

        var client = new test.mongodb.MongoClient(uri, { useUnifiedTopology: true });
        await client.connect();
        var db = await client.db(dbName);

        // This will cause an authentication all the way to the backend using the provided credentials.
        var coll = await db.command({find: "this_should_not_exist"});
        client.close();
    }

    async function can_create_user(nosql, dbPrefix) {
        var dbName = nosql.dbName();

        var user = {
            user: "bob",
            pwd: "bobspwd",
            roles: [
                {db: dbName, role: "userAdmin"},
                {db: dbName, role: "readWrite"},
            ],
            mechanisms: ["SCRAM-SHA-1", "SCRAM-SHA-256"]
        };

        await create_user(user, nosql);
        var fetched_user = await fetch_user(user, nosql);

        // Remove fields that won't be find on the fetched one.
        delete user.pwd;

        assert.deepEqual(user, fetched_user);

        var rv = await mariadb.query("SELECT user FROM mysql.user WHERE user = '" + dbPrefix + "bob'");

        assert.equal(rv.length, 1);

        await smoke_test_user("bob", "bobspwd", dbName);
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
