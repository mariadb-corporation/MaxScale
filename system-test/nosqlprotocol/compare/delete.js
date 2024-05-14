/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

const fs = require('fs');
const test = require('../test/nosqltest.js');

const name = "delete";

var mongo;
var nosql_d;
var nosql_dc;
var nosql_c;
var cars;

const N = 5;

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

async function delete_with_command(db, heading, command, create_index) {
    var rv;
    var total = 0;
    for (var i = 0; i < N; ++i) {
        await prepare(db, create_index);
        var start = new Date();
        rv = await db.runCommand(command);
        var stop = new Date();

        total += (stop - start);
    }

    console.log(heading + ": " + total/N + "ms, ", rv);
}

async function delete_some(db, heading, create_index)
{
    await prepare(db, create_index);

    var command = {
        "delete": name,
        deletes: [{q: { Make: "Toyota" }, limit: 0}]
    };

    await delete_with_command(db, heading, command, create_index);
}

async function delete_one(db, heading, create_index)
{
    await prepare(db, create_index);

    var command = {
        "delete": name,
        deletes: [{q: { "_id": { "$eq": 4711 }}, limit: 0}]
    };

    await delete_with_command(db, heading, command, create_index);
}

async function delete_by_id(db, heading, create_index)
{
    await prepare(db, create_index);

    var command = {
        "delete": name,
        deletes: [{q: { "_id": 4711 }, limit: 0}]
    };

    await delete_with_command(db, heading, command, create_index);
}

async function delete_many_by_id(deletes, db, heading, create_index) {
    await prepare(db, create_index);

    var command = {
        "delete": name,
        deletes: deletes,
        ordered: false
    };

    await delete_with_command(db, heading, command, create_index);
}

async function compare_delete_some() {
    console.log("Delete some.\n");

    await delete_some(mongo,    "Mongo             ", false);
    await delete_some(mongo,    "Mongo (indexed)   ", true);
    await delete_some(nosql_d,  "NoSQL (non-cached)");
    await delete_some(nosql_dc, "NoSQL (cached)    ");
    await delete_some(nosql_c,  "Cached NoSQL      ");
}

async function compare_delete_one() {
    console.log("Delete one by Query.\n");

    await delete_one(mongo,    "Mongo             ", false);
    await delete_one(mongo,    "Mongo (indexed)   ", true);
    await delete_one(nosql_d,  "NoSQL (non-cached)");
    await delete_one(nosql_dc, "NoSQL (cached)    ");
    await delete_one(nosql_c,  "Cached NoSQL      ");
}

async function compare_delete_by_id() {
    console.log("Delete one by id.\n");

    await delete_by_id(mongo,    "Mongo             ", false);
    await delete_by_id(mongo,    "Mongo (indexed)   ", true);
    await delete_by_id(nosql_d,  "NoSQL (non-cached)");
    await delete_by_id(nosql_dc, "NoSQL (cached)    ");
    await delete_by_id(nosql_c,  "Cached NoSQL      ");
}

async function compare_delete_many_by_id() {
    console.log("Delete many by id.\n");

    var ids = [];

    for (var i = 0; i < cars.length; ++i) {
        var car = cars[i];

        if (car.Make == 'Toyota') {
            ids.push({_id: car._id});
        }
    }

    var deletes = [];
    deletes.push({q: { "$or": ids}, limit:0});

    await delete_many_by_id(deletes, mongo,    "Mongo             ", false);
    await delete_many_by_id(deletes, mongo,    "Mongo (indexed)   ", true);
    await delete_many_by_id(deletes, nosql_d,  "NoSQL (non-cached)");
    await delete_many_by_id(deletes, nosql_dc, "NoSQL (cached)    ");
    await delete_many_by_id(deletes, nosql_c,  "Cached NoSQL      ");
}

async function run() {
    mongo = await test.NoSQL.create("compare", 27017);
    nosql_d = await test.NoSQL.create("compare_d", 17017);
    nosql_dc = await test.NoSQL.create("compare_dc", 17018);
    nosql_c = await test.NoSQL.create("compare_c", 17019);

    cars = load_cars();

    await compare_delete_some();
    console.log();

    await compare_delete_one();
    console.log();

    await compare_delete_by_id();
    console.log();

    await compare_delete_many_by_id();
    console.log();

    nosql_c.close();
    nosql_dc.close();
    nosql_d.close();
    mongo.close();
}

run();
