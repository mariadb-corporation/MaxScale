/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// https://docs.mongodb.com/manual/reference/command/getFreeMonitoringStatus

const assert = require('assert');
const test = require('./nosqltest')

const name = "getFreeMonitoringStatus";

describe(name, function () {
    this.timeout(test.timeout);

    let mng;
    let mxs;

    function check_fields(doc) {
        assert.notEqual(doc.state, undefined);
        assert.notEqual(doc.ok, undefined);
    }

    /*
     * MOCHA
     */
    before(async function () {
        mng = await test.MDB.create(test.MngMongo, "admin");
        mxs = await test.MDB.create(test.MxsMongo, "admin");
    });

    it('Command implemented.', async function () {
        var rv1 = await mxs.runCommand({getFreeMonitoringStatus: 1});
        var rv2 = await mxs.runCommand({getFreeMonitoringStatus: 1});

        assert.deepEqual(rv1, rv2);
    });

    after(async function () {
        if (mng) {
            await mng.close();
        }

        if (mxs) {
            await mxs.close();
        }
    });
});
