/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-06-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

const fs = require('fs');
const test = require('../test/nosqltest.js');

const name = "find";

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

async function insert(db, docs)
{
    var command = {
        insert: name,
        documents: docs
    };

    await db.runCommand(command);
}


async function reset(db)
{
    await create(db);
    await delete_all(db);
}

async function find_with_command(db, heading, command, n) {
    if (!n) {
        n = 1;
    }

    var start = new Date();
    for (var i = 0; i < n; ++i) {
        var rv = await db.runCommand(command);
    }
    var stop = new Date();

    console.log(heading + ": " + (stop - start)/n + ", " + rv.cursor.firstBatch.length);
}

async function fetch_all(db, heading) {
    var command = {
        find: name,
        batchSize: 10000
    };
    var start = new Date();
    var rv = await db.runCommand(command);

    var total = rv.cursor.firstBatch.length;

    if (rv.cursor.id) {
        while (rv.cursor.id) {
            command = {
                getMore: rv.cursor.id,
                collection: name
            };

            rv = await db.runCommand(command);

            total += rv.cursor.nextBatch.length;
        }
    }

    var stop = new Date();

    console.log(heading + ": " + (stop - start) + ", " + total);
}

async function find_all(db, heading) {
    var command = {
        find: name
    };

    await find_with_command(db, heading, command);
}

async function find_some(db, heading) {
    var command = {
        find: name,
        filter : { "Make": "Toyota" },
        batchSize: 10000
    };

    await find_with_command(db, heading, command);
}

async function find_one(db, heading) {
    var command = {
        find: name,
        filter : { "_id": { "$eq": 4711 }}
    };

    await find_with_command(db, heading, command, 1000);
}

async function find_by_id(db, heading) {
    var command = {
        find: name,
        filter : { "_id": 4711 }
    };

    await find_with_command(db, heading, command, 1000);
}

async function compare_find_all(mongo, nosql) {
    console.log("Find all\n");

    await find_all(mongo, "Mongo");
    await find_all(nosql, "NoSQL");
}

async function compare_fetch_all(mongo, nosql) {
    console.log("Fetch all\n");

    await fetch_all(mongo, "Mongo");
    await fetch_all(nosql, "NoSQL");
}

async function compare_find_some(mongo, nosql) {
    console.log("Find some\n");

    await find_some(mongo, "Mongo");
    await find_some(nosql, "NoSQL");
}

async function compare_find_one(mongo, nosql) {
    console.log("Find one\n");

    await find_one(mongo, "Mongo");
    await find_one(nosql, "NoSQL");
}

async function compare_find_by_id(mongo, nosql) {
    console.log("Find one (index)\n");

    await find_by_id(mongo, "Mongo");
    await find_by_id(nosql, "NoSQL");
}

async function run() {
    var mongo = await await test.MDB.create(test.MngMongo, "compare");
    var nosql = await await test.MDB.create(test.MxsMongo, "compare");

    const cars = load_cars();

    await reset(mongo);
    await insert(mongo, cars);

    await reset(nosql);
    await insert(nosql, cars);

    await compare_find_all(mongo, nosql);
    console.log();
    await compare_fetch_all(mongo, nosql);
    console.log();
    await compare_find_some(mongo, nosql);
    console.log();
    await compare_find_one(mongo, nosql);
    console.log();
    await compare_find_by_id(mongo, nosql);
    console.log();

    nosql.close();
    mongo.close();

}

run();
