/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

const fs = require('fs');
const test = require('../test/nosqltest.js');

const name = "find";

var mongo;
var nosql_d;
var nosql_dc;
var nosql_c;
var cars;

const N = 100;

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

async function drop(db) {
    var command = {
        drop: name,
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
    await delete_all(db);
    await drop(db);
    await create(db);
}

async function prepare(db, create_index) {
    await reset(db);
    if (create_index) {
        var command = {
            createIndexes: name,
            indexes: [ { key: { Make: 1}, name: "Make" }]
        };

        await db.runCommand(command);
    }
    await insert(db, cars);
}

async function find_with_command(db, heading, command, create_index) {
    await prepare(db, create_index);

    var start = new Date();
    var rv = await db.runCommand(command);
    var stop = new Date();

    cold_time = stop - start;
    cold_count = rv.cursor.firstBatch.length;

    start = new Date();
    for (var i = 0; i < N; ++i) {
        var rv = await db.runCommand(command);
    }
    stop = new Date();

    console.log(heading + ": " + (stop - start)/N + "ms, " + rv.cursor.firstBatch.length
                + ", (" + cold_time + "ms, " + cold_count + ")");
}

async function find_all(db, heading) {
    var command = {
        find: name,
        batchSize: 10000
    };

    await find_with_command(db, heading, command);
}

async function find_default(db, heading) {
    var command = {
        find: name
    };

    await find_with_command(db, heading, command);
}

async function find_some(db, heading, create_index) {
    var command = {
        find: name,
        filter : { "Make": "Toyota" },
        batchSize: 10000
    };

    await find_with_command(db, heading, command, create_index);
}

async function find_one(db, heading) {
    var command = {
        find: name,
        filter : { "_id": { "$eq": 4711 }}
    };

    await find_with_command(db, heading, command);
}

async function find_by_id(db, heading) {
    var command = {
        find: name,
        filter : { "_id": 4711 }
    };

    await find_with_command(db, heading, command);
}

async function compare_find_default() {
    console.log("Find default; at most 101 documents returned in batch\n");

    await find_default(mongo,    "Mongo             ");
    await find_default(nosql_d,  "NoSQL (non-cached)");
    await find_default(nosql_dc, "NoSQL (cached)    ");
    await find_default(nosql_c,  "Cached NoSQL      ");
}

async function compare_find_all() {
    console.log("Find all; at most 10000 documents returned in batch\n");

    await find_all(mongo,    "Mongo             ");
    await find_all(nosql_d,  "NoSQL (non-cached)");
    await find_all(nosql_dc, "NoSQL (cached)    ");
    await find_all(nosql_c,  "Cached NoSQL      ");
}

async function compare_find_some() {
    console.log("Find some, all cars whose make is Toyota.\n");

    await find_some(mongo,    "Mongo             ", false);
    await find_some(mongo,    "Mongo (indexed)   ", true);
    await find_some(nosql_d,  "NoSQL (non-cached)");
    await find_some(nosql_dc, "NoSQL (cached)    ");
    await find_some(nosql_c,  "Cached NoSQL      ");
}

async function compare_find_one() {
    console.log("Find one; non-indexed query\n");

    await find_one(mongo,    "Mongo             ");
    await find_one(nosql_d,  "NoSQL (non-cached)");
    await find_one(nosql_dc, "NoSQL (cached)    ");
    await find_one(nosql_c,  "Cached NoSQL      ");
}

async function compare_find_by_id() {
    console.log("Find one; by id (i.e. indexed)\n");

    await find_by_id(mongo,    "Mongo             ");
    await find_by_id(nosql_d,  "NoSQL (non-cached)");
    await find_by_id(nosql_dc, "NoSQL (cached)    ");
    await find_by_id(nosql_c,  "Cached NoSQL      ");
}

async function run() {
    mongo = await test.NoSQL.create("compare", 27017);
    nosql_d = await test.NoSQL.create("compare_d", 17017);
    nosql_dc = await test.NoSQL.create("compare_dc", 17018);
    nosql_c = await test.NoSQL.create("compare_c", 17019);

    cars = load_cars();

    console.log();

    await compare_find_default();
    console.log();

    await compare_find_all();
    console.log();

    await compare_find_some();
    console.log();

    await compare_find_one();
    console.log();

    await compare_find_by_id();
    console.log();

    nosql_c.close();
    nosql_dc.close();
    nosql_d.close();
    mongo.close();
}

run();
