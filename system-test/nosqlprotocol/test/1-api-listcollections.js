/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// https://docs.mongodb.com/manual/reference/command/listCollections

const assert = require('assert');
const test = require('./nosqltest')

const name = "listCollections";

describe(name, function () {
    this.timeout(test.timeout);

    let mxs;
    let mng;

    /*
     * MOCHA
     */
    before(async function () {
        mng = await test.MDB.create(test.MngMongo, name);
        mxs = await test.MDB.create(test.MxsMongo, name);

        await mng.runCommand({dropDatabase: 1});
        await mxs.runCommand({dropDatabase: 1});
    });

    it('Can list collections from a non-existing database.', async function () {
        var rv1 = await mng.runCommand({"listCollections": 1});
        var rv2 = await mxs.runCommand({"listCollections": 1});

        assert.equal(rv1.ok, 1);
        assert.deepEqual(rv2, rv1);
    });

    it('Can list collections from database with collections.', async function () {
        var collections = [ "a", "b", "c" ];

        var n = 0;

        for (var i in collections)
        {
            ++n;

            var collection = collections[i];

            var rv1 = await mng.insert_n(collection, 10);
            assert.equal(rv1.ok, 1);
            var rv2 = await mxs.insert_n(collection, 10);
            assert.equal(rv2.ok, 1);

            rv1 = await mng.runCommand({"listCollections": 1, nameOnly: true});
            rv2 = await mxs.runCommand({"listCollections": 1, nameOnly: true});

            assert.equal(rv1.cursor.firstBatch.length, n);
            assert.equal(rv2.cursor.firstBatch.length, n);
        }
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
