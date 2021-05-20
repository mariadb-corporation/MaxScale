/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-01-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// https://docs.mongodb.com/drivers/node/usage-examples/updateOne/

const assert = require('assert');
const test = require('./mongotest');

const name = 'lib-update';

describe(name, function () {
    this.timeout(test.timeout);

    let mxs;
    let collection;
    const N = 20;

    before(async function () {
        mxs = await test.MDB.create(test.MxsMongo);

        const database = mxs.db;
        collection = database.collection(name);

        var documents = [];

        for (var i = 0; i < N; ++i)
        {
            var doc = { field: i };
            documents.push(doc);
        }

        const result = await collection.insertMany(documents);

        assert.strictEqual(result.insertedCount, documents.length, "Should be able to insert documents.");
    });

    it('$set', async function () {

        const filter = {
            field: Math.round(N/2)
        };

        const update = {
            "$set": {
                field: N + 1
            }
        };

        var result;

        result = await collection.updateOne(filter, update);
        assert.strictEqual(result.matchedCount, 1);

        var doc = await collection.findOne({field: N + 1});
        assert(doc);
        assert.strictEqual(doc.field, N + 1);

        update["$set"].field = N + 2;
        result = await collection.updateMany({}, update);
        assert.strictEqual(result.modifiedCount, N);

        var cursor = collection.find();

        await cursor.forEach(function(doc) {
            assert.strictEqual(doc.field, N + 2);
        });
    });

    after(function () {
        if (mxs) {
            mxs.close();
        }
    });
});
