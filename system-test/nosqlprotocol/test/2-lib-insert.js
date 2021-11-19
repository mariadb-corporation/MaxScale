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

// https://docs.mongodb.com/drivers/node/usage-examples/insertOne/
// https://docs.mongodb.com/drivers/node/usage-examples/insertMany/

const assert = require('assert');
const fs = require('fs');
const test = require('./nosqltest');
const mongodb = test.mongodb;

const name = 'lib-insert';

describe(name, function () {
    this.timeout(test.timeout);

    let mng;
    let mxs;

    before(async function () {
        mng = await test.MDB.create(test.MngMongo);
        mxs = await test.MDB.create(test.MxsMongo);
    });

    async function delete_all () {
        await mng.deleteAll("cars");
        await mxs.deleteAll("cars");
    }

    async function insertOne(db) {
        const cars = db.collection("cars");

        // create a document to be inserted
        const doc = { _id: 10000, Year: 2022, Make: "Caterham", Model: "Super Seven 1600", Category: "Sport" };
        const result = await cars.insertOne(doc);

        return result;
    }

    it('Can insert one document', async function () {
        await delete_all();

        const rv1 = await insertOne(mng.db);
        const rv2 = await insertOne(mxs.db);

        assert.deepEqual(rv1.result, rv2.result);
    });

    async function insertMany(db) {
        const cars = db.collection("cars");

        // create an array of documents to insert
        const docs = [
            { _id: 10001, Year: 2022, Make: "Caterham", Model: "Seven 270", Category: "Sport" },
            { _id: 10002, Year: 2022, Make: "Caterham", Model: "Seven 310", Category: "Sport" },
            { _id: 10003, Year: 2022, Make: "Caterham", Model: "Seven 360", Category: "Sport" },
        ];

        const options = { ordered: true };
        const result = await cars.insertMany(docs, options);

        return result;
    }

    it('Can insert many documents', async function () {
        await delete_all();

        const rv1 = await insertMany(mng.db);
        const rv2 = await insertMany(mxs.db);

        assert.deepEqual(rv1.result, rv2.result);
    });

    async function insertLots(db) {
        const cars = db.collection("cars");

        var doc = JSON.parse(fs.readFileSync("test/cars.json", "utf8"));
        var docs = doc.cars;

        const options = { ordered: true };
        const result = await cars.insertMany(docs, options);

        return result;
    }

    it('Can insert lots of documents', async function () {
        await delete_all();

        const rv1 = await insertLots(mng.db);
        const rv2 = await insertLots(mxs.db);

        assert.deepEqual(rv1.result, rv2.result);
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
