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

const name = "usersInfo";

describe(name, function () {
    this.timeout(test.timeout);

    let nosql;
    let mariadb;

    const userName = "nosql_user";
    const password = "nosql_password";
    var mariadbPassword;

    /*
     * MOCHA
     */
    before(async function () {
        nosql = await test.NoSQL.create("nosql");
        mariadb = await test.MariaDB.createConnection();

        try {
            await nosql.runCommand({dropUser: userName});
        }
        catch (x) {}

        await nosql.runCommand({createUser: userName, pwd: password, roles: []});

    });

    it('Can call usersInfo without showCredentials.', async function () {
        var ui = await nosql.runCommand({usersInfo: 1});

        var users = ui.users;

        users.forEach(function (user) {
            assert.equal(user.credentials, undefined);
            assert.equal(user.mariadb, undefined);
        });
    });

    it('Can call usersInfo with showCredentials.', async function () {
        var ui = await nosql.runCommand({usersInfo: 1, showCredentials: true});

        var users = ui.users;

        users.forEach(function (user) {
            assert.notEqual(user.credentials, undefined);
            assert.notEqual(user.mariadb, undefined);
            assert.notEqual(user.mariadb.password, undefined);
        });
    });

    it('Can get MariaDB password.', async function () {
        var rows = await mariadb.query("SELECT password FROM mysql.user WHERE user = '"
                                       + "nosql." + userName + "'");
        assert.equal(rows.length, 1);

        var mariadbPassword = rows[0].Password;

        var ui = await nosql.runCommand({usersInfo: userName, showCredentials: true});

        var users = ui.users;
        assert.equal(users.length, 1);

        var user = users[0];
        assert.notEqual(user.mariadb, undefined);

        assert.equal(user.mariadb.password.toUpperCase(), mariadbPassword.toUpperCase());
    });

    after(async function () {
        if (nosql) {
            await nosql.close();
        }

        if (mariadb) {
            await mariadb.end();
        }
    });

});
