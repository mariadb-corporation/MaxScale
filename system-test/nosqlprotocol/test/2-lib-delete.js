/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// https://docs.mongodb.com/drivers/node/usage-examples/deleteOne
// https://docs.mongodb.com/drivers/node/usage-examples/deleteMany

const assert = require('assert');
const test = require('./nosqltest');
const mongodb = test.mongodb;

const name = 'lib-delete';

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

    async function delete_one(db, name) {
        const cars = db.collection("cars");

        const query = { Make: name };

        return await cars.deleteOne(query);
    }

    it('Can delete one document', async function () {
        var toyota = "Toyota";

        const rv1 = await delete_one(mng.db, toyota);
        const rv2 = await delete_one(mxs.db, toyota);

        assert.equal(rv1.deletedCount, 1);
        assert.equal(rv2.deletedCount, rv1.deletedCount);
    });

    async function delete_many(db, name) {
        const cars = db.collection("cars");

        const query = { Make: name };

        return await cars.deleteMany(query);
    }

    async function find(db, name) {
        const cars = db.collection("cars");

        const query = { Make: name };

        return await cars.find(query);
    }

    it('Can delete many documents', async function () {
        var toyota = "Toyota";

        const rv1 = await delete_many(mng.db, toyota);
        const rv2 = await delete_many(mxs.db, toyota);

        assert.notEqual(rv1.deletedCount, 0);
        assert.equal(rv2.deletedCount, rv1.deletedCount);

        var cursor1 = await find(mng.db, toyota);
        var cursor2 = await find(mxs.db, toyota);

        assert.equal(await cursor1.count(), 0);
        assert.equal(await cursor2.count(), 0);
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
