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

// https://docs.mongodb.com/drivers/node/usage-examples/findOne/
// https://docs.mongodb.com/drivers/node/usage-examples/findMany

const assert = require('assert');
const fs = require('fs');
const test = require('./nosqltest');
const mongodb = test.mongodb;

const name = 'lib-find';

describe(name, function () {
    this.timeout(test.timeout);

    let mng;
    let mxs;

    async function delete_cars() {
        await mng.delete_cars();
        await mxs.delete_cars();
    }

    async function insert_cars() {
        await mng.insert_cars();
        await mxs.insert_cars();
    }

    before(async function () {
        mng = await test.MDB.create(test.MngMongo);
        mxs = await test.MDB.create(test.MxsMongo);

        await delete_cars();
        await insert_cars();
    });

    async function find_one(db) {
        const cars = db.collection("cars");

        // Query for a car whose maker is 'Toyota'
        const query = { Make: "Toyota" };

        const options = {
            // Sort matched document in descending order by year.
            sort: { Year: -1 },
            // Select only the 'Model' and 'Category' fields
            projection: { _id: 0, Model: 1, Category: 1 }
        };

        const car = await cars.findOne(query, options);

        return car;
    }

    it('Can find one document', async function () {
        const rv1 = await find_one(mng.db);
        const rv2 = await find_one(mxs.db);

        assert.notEqual(rv1.Model, undefined);
        assert.deepEqual(rv1.result, rv2.result);
    });

    async function find_many(db) {
        const cars = db.collection("cars");

        // Query for cars that were introduced before 2010
        const query = { Year: { $lt: 2010 }};

        const options = {
            // Sort in ascending order according to maker.
            sort: { Make: 1 },

            // Include only maker, model and category in returned documents.
            projection: { Make: 1, Model: 1, Category: 1 }
        };

        return await cars.find(query, options);
    }

    it('Can find many documents', async function () {
        const cursor1 = await find_many(mng.db);
        const cursor2 = await find_many(mxs.db);

        assert.notEqual(await cursor1.count(), 0);
        assert.equal(await cursor2.count(), await cursor2.count());

        assert.deepEqual(await cursor2.toArray(), await cursor1.toArray());
    });

    after(function () {
        if (mng) {
            mng.close();
        }

        if (mxs) {
            mxs.close();
        }
    });
});
