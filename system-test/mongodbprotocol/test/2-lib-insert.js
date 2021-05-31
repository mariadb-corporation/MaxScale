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

const assert = require('assert');
const test = require('./mongotest');
const mongodb = test.mongodb;

const name = 'lib-insert';

describe(name, function () {
    this.timeout(test.timeout);

    let mng;
    let mxs;

    before(async function () {
        mng = await test.MDB.create(test.MngMongo, name);
        mxs = await test.MDB.create(test.MxsMongo, name);
    });

    async function insertOne(db) {
        const movies = db.collection("movies");

        // create a document to be inserted
        const doc = { _id: mongodb.ObjectId(), name: "Red", town: "kanto" };
        const result = await movies.insertOne(doc);

        return result;
    }

    // https://docs.mongodb.com/drivers/node/usage-examples/insertOne/
    it('Can insert one document', async function () {
        const rv1 = await insertOne(mng.db);
        const rv2 = await insertOne(mxs.db);

        assert.deepEqual(rv1.result, rv2.result);
    });

    async function insertMany(db) {
        const movies = db.collection("movies");

        // create an array of documents to insert
        const docs = [
            { name: "Red", town: "Kanto" },
            { name: "Blue", town: "Kanto" },
            { name: "Leon", town: "Galar" }
        ];

        const options = { ordered: true };
        const result = await movies.insertMany(docs, options);

        return result;
    }

    // https://docs.mongodb.com/drivers/node/usage-examples/insertMany/
    it('Can insert many document', async function () {
        const rv1 = await insertMany(mng.db);
        const rv2 = await insertMany(mxs.db);

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
