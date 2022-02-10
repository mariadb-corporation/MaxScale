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

const name = "grantRolesToUser";

describe(name, function () {
    this.timeout(test.timeout);

    let nosql2;
    let nosql1;
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

    function check(role, expected, found, exact) {
        var success = true;

        if (exact) {
            success = JSON.stringify(expected) === JSON.stringify(found);
        }
        else {
            // There are several roles that correspond to privileges that
            // have to be GRANTed to '*.*'. Thus, when checking for such
            // privileges corresponding to a particlar role, we can only
            // check whether the expected privileges are present.
            for (i in expected) {
                if (found.indexOf(expected[i]) == -1) {
                    success = false;
                    break;
                }
            }
        }

        if (!success) {
            console.log("Role:     ", role);
            console.log("Expected: ", expected);
            console.log("Found   : ", found);
            assert.fail();
        }
    }

    async function check_privileges(user, roles) {
        var grants = await mariadb.fetch_grants_for(user);

        for (var i in roles) {
            var role = roles[i];
            var db;

            if (role.db == "admin") {
                db = "*.*";
            }
            else {
                db = "`" + role.db + "`.*";
            }

            var privileges = test.privileges_by_role[role.role];
            var expected = privileges.db;
            var found = grants[db]

            check(role, expected, found, true);

            if (role.db == "admin") {
                expected = privileges.adminDb;

                if (expected) {
                    found = grants["*.*"];

                    check(role, expected, found, false);
                }
            }
        }
    }

    async function grant_roles(user, roles, nosql) {
        await nosql.runCommand({grantRolesToUser: user, roles });
    }

    async function test_role_granting(nosql_name, mariadb_name, nosql) {
        var roles = [];

        roles.push({db: "test1", role: "dbAdmin"});
        console.log(roles[roles.length - 1]);
        await grant_roles(nosql_name, roles, nosql);
        await check_privileges(mariadb_name, roles);

        roles.push({db: "test2", role: "dbOwner"});
        console.log(roles[roles.length - 1]);
        await grant_roles(nosql_name, roles, nosql);
        await check_privileges(mariadb_name, roles);

        roles.push({db: "test3", role: "read"});
        console.log(roles[roles.length - 1]);
        await grant_roles(nosql_name, roles, nosql);
        await check_privileges(mariadb_name, roles);

        roles.push({db: "test4", role: "readWrite"});
        console.log(roles[roles.length - 1]);
        await grant_roles(nosql_name, roles, nosql);
        await check_privileges(mariadb_name, roles);

        roles.push({db: "test5", role: "userAdmin"});
        console.log(roles[roles.length - 1]);
        await grant_roles(nosql_name, roles, nosql);
        await check_privileges(mariadb_name, roles);

        roles.push({db: "admin", role: "root"});
        console.log(roles[roles.length - 1]);
        await grant_roles(nosql_name, roles, nosql);
        await check_privileges(mariadb_name, roles);
    }

    /*
     * MOCHA
     */
    before(async function () {
        nosql1 = await test.NoSQL.create("nosql");
        mariadb = await test.MariaDB.createConnection();
    });

    it('Can grant roles.', async function () {
        var user = {
            user: "bob",
            pwd: "bobspwd",
            roles: []
        };

        await create_user(user, nosql1);

        await test_role_granting(user.user, nosql1.dbName() + "." + user.user, nosql1);
    });

    it('Can grant roles to user in "mariadb".', async function () {
        nosql2 = await test.NoSQL.create("mariadb");

        var user = {
            user: "bob",
            pwd: "bobspwd",
            roles: []
        };

        await create_user(user, nosql2);

        await test_role_granting(user.user, user.user, nosql2);
    });

    after(async function () {
        if (nosql2) {
            await drop_user("bob", nosql2);
            await nosql2.close();
        }
        if (nosql1) {
            await drop_user("bob", nosql1);
            await nosql1.close();
        }

        if (mariadb) {
            await mariadb.end();
        }
    });
});
