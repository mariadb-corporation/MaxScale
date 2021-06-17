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

const fs = require('fs');
const test = require('../test/nosqltest.js');

const name = "update";

function load_cars() {
    var doc = JSON.parse(fs.readFileSync("../test/cars.json", "utf8"));

    return doc.cars;
}

async function create(db) {
    var command = {
        create: name,
    };
    await db.ntRunCommand(command);
}

async function delete_all(db)
{
    var command = {
        delete: name,
        deletes: [{q:{}, limit:0}]
    };
    await db.runCommand(command);
}

async function reset(db)
{
    await create(db);
    await delete_all(db);
}

async function insert(db, docs)
{
    var command = {
        insert: name,
        documents: docs
    };

    await db.runCommand(command);
}

async function update(db, heading)
{
    var command = {
        update: name,
        updates: [{q: { Make: "Toyota" }, u: { $set: { Make: "Toyoda" }}, multi: true}]
    };

    var start = new Date();
    var rv = await db.runCommand(command);
    var stop = new Date();

    console.log(heading + ": " + (stop - start), rv);
}

async function compare_update(mongo, nosql, cars) {
    console.log("Update.\n");

    await update(mongo, "Mongo");
    await update(nosql, "NoSQL");
}

async function run() {
    var mongo = await await test.MDB.create(test.MngMongo, "compare");
    var nosql = await await test.MDB.create(test.MxsMongo, "compare");

    const cars = load_cars();

    await reset(mongo);
    await insert(mongo, cars);

    await reset(nosql);
    await insert(nosql, cars);

    await compare_update(mongo, nosql);

    nosql.close();
    mongo.close();
}

run();
