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
        mng = await test.MDB.create(test.MngMongo, name);
        mxs = await test.MDB.create(test.MxsMongo, name);

        await delete_cars();
        await insert_cars();
    });

    async function update_one(db) {
        const cars = db.collection("cars");

        const filter = {
            Make: "Toyota"
        };

        const update = {
            "$set": {
                Make: "Toyoda"
            }
        };

        return await cars.updateOne(filter, update);
    }

    async function find_one(db) {
        const cars = db.collection("cars");

        const filter = {
            Make: "Toyoda"
        };

        return await cars.findOne(filter);
    }

    it('Can $set on one', async function () {
        var rv1 = await update_one(mng.db);
        var rv2 = await update_one(mxs.db);

        assert.equal(rv1.matchedCount, 1);
        assert.equal(rv2.matchedCount, 1);

        var car1 = await find_one(mng.db);
        var car2 = await find_one(mxs.db);

        assert.deepEqual(car1, car2);
    });

    async function find_many(db, make) {
        const cars = db.collection("cars");

        const filter = {
            Make: make
        };

        return await cars.find(filter);
    }

    async function update_many(db, from, to) {
        const cars = db.collection("cars");

        const filter = {
            Make: from
        };

        const update = {
            "$set": {
                Make: to
            }
        };

        return await cars.updateMany(filter, update);
    }

    it('Can $set on many', async function () {
        const toyota = "Toyota";
        const toyoda = "Toyoda";

        // Let's change all "Toyota" to "Toyoda"
        var rv1 = await update_many(mng.db, toyota, toyoda);
        var rv2 = await update_many(mxs.db, toyota, toyoda);

        // More than 1 should be changed.
        assert.notEqual(rv1.matchedCount, 0);
        assert.notEqual(rv1.matchedCount, 1);
        assert.equal(rv2.matchedCount, rv1.matchedCount);

        var cursor1 = await find_many(mng.db, toyoda);
        var cursor2 = await find_many(mxs.db, toyoda);

        var count1 = await cursor1.count();
        var count2 = await cursor2.count();

        assert.notEqual(count1, 0);
        assert.equal(count2, count1);

        // Let's change "Toyoda" back to "Toyota"
        var rv1 = await update_many(mng.db, toyoda, toyota);
        var rv2 = await update_many(mxs.db, toyoda, toyota);

        var cursor1 = await find_many(mng.db, toyoda);
        var cursor2 = await find_many(mxs.db, toyoda);

        var count1 = await cursor1.count();
        var count2 = await cursor2.count();

        // No "Toyoda"s should be found.
        assert.equal(count1, 0);
        assert.equal(count2, count2);
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
