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

const name = "mxsUpdateUser";
const db = "nosql";

describe(name, function () {
    this.timeout(test.timeout);

    let nosql;
    let conn;

    async function fetch_bob() {
        var rv = await nosql.runCommand({usersInfo: "bob"});
        var users = rv.users;
        var user = users[0];

        assert.equal(user.db, db);
        assert.equal(user._id, db + ".bob");
        assert.notEqual(user.userId, undefined);

        delete user._id;
        delete user.db;
        delete user.userId;

        return user;
    }

    /*
     * MOCHA
     */
    before(async function () {
        nosql = await test.NoSQL.create(db);

        await nosql.ntRunCommand({dropUser: "bob"});

        await nosql.runCommand({createUser: "bob",
                                pwd: "bobspwd",
                                mechanisms: ["SCRAM-SHA-1", "SCRAM-SHA-256"],
                                roles: []});
    });

    it('Can update custom data.', async function () {
        var customData = {hello: "world"};

        await nosql.runCommand({updateUser: "bob", customData: customData });

        var bob = await fetch_bob()

        assert.deepEqual(bob.customData, customData);
    });

    it('Can update roles.', async function () {
        var roles = [{db:"db1", role: "read"}, {db: "db2", role: "readWrite"}];

        await nosql.runCommand({updateUser: "bob", roles: roles});

        var bob = await fetch_bob();

        assert.deepEqual(bob.roles, roles);
    });

    it('Can reduce mechanisms without providing password.', async function () {
        var mechanisms = ["SCRAM-SHA-1"];

        await nosql.runCommand({updateUser: "bob", mechanisms: mechanisms});

        var bob = await fetch_bob();

        assert.deepEqual(bob.mechanisms, mechanisms);
    });

    it('Cannot expand mechanisms without providing password.', async function () {
        var mechanisms = ["SCRAM-SHA-1", "SCRAM-SHA-256"];

        try {
            await nosql.runCommand({updateUser: "bob".user, mechanisms: mechanisms});
            assert.fail("Could expand mechanisms without password.");
        }
        catch (x) {
        }
    });

    it('Can expand mechanisms when providing password.', async function () {
        var mechanisms = ["SCRAM-SHA-1", "SCRAM-SHA-256"];

        await nosql.runCommand({updateUser: "bob", pwd: "bobs2ndpwd", mechanisms: mechanisms});

        var bob = await fetch_bob();

        assert.deepEqual(bob.mechanisms, mechanisms);
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
