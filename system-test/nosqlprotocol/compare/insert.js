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

const fs = require('fs');
const test = require('../test/nosqltest.js');

const name = "insert";

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

async function insert_batch(db, docs, heading, ordered)
{
    var command = {
        insert: name,
        documents: docs,
        ordered: ordered
    };

    await delete_all(db);
    var start = new Date();
    await db.runCommand(command);
    var stop = new Date();

    var s = heading + ", ordered = ";

    if (ordered) {
        s += "true";
    }
    else {
        s += "false";
    }

    s += ": ";

    console.log(s, (stop - start));
}

async function compare_insert_batch(mongo, nosql, cars) {
    console.log("Inserting " + cars.length + " documents as one batch.\n");

    //
    // MongoDB
    //
    await reset(mongo);

    await insert_batch(mongo, cars, "Mongo", true);
    await insert_batch(mongo, cars, "Mongo", false);

    //
    // NoSQL
    //
    await reset(nosql);

    var config = {
        ordered_insert_behavior: undefined
    };

    // Default
    config.ordered_insert_behavior = "default";
    await nosql.adminCommand({mxsSetConfig: config});

    await insert_batch(nosql, cars, "nosqlprotocol(default)", true);
    await insert_batch(nosql, cars, "nosqlprotocol(default)", false);

    // Atomic
    config.ordered_insert_behavior = "atomic";
    await nosql.adminCommand({mxsSetConfig: config});

    await insert_batch(nosql, cars, "nosqlprotocol(atomic)", true);
    await insert_batch(nosql, cars, "nosqlprotocol(atomic)", false);
}

async function insert_individual(db, docs, n, heading, ordered)
{
    await delete_all(db);

    var start = new Date();

    for (var i = 0; i < n; ++i) {
        var command = {
            insert: name,
            documents: [docs[i]],
            ordered: ordered
        };

        await db.runCommand(command);
    }
    var stop = new Date();

    var s = heading + ", ordered = ";

    if (ordered) {
        s += "true";
    }
    else {
        s += "false";
    }

    s += ": ";

    console.log(s, (stop - start));
}


async function compare_insert_individual(mongo, nosql, cars, n) {
    console.log("Inserting " + n + " documents individually.\n");

    //
    // MongoDB
    //
    await reset(mongo);

    await insert_individual(mongo, cars, n, "Mongo", true);
    await insert_individual(mongo, cars, n, "Mongo", false);

    //
    // NoSQL
    //
    await reset(nosql);

    var config = {
        ordered_insert_behavior: undefined
    };

    // Default
    config.ordered_insert_behavior = "default";
    await nosql.adminCommand({mxsSetConfig: config});

    await insert_individual(nosql, cars, n, "nosqlprotocol(default)", true);
    await insert_individual(nosql, cars, n, "nosqlprotocol(default)", false);

    // Atomic
    config.ordered_insert_behavior = "atomic";
    await nosql.adminCommand({mxsSetConfig: config});

    await insert_individual(nosql, cars, n, "nosqlprotocol(atomic)", true);
    await insert_individual(nosql, cars, n, "nosqlprotocol(atomic)", false);
}

async function run() {
    var mongo = await await test.MDB.create(test.MngMongo, "compare");
    var nosql = await await test.MDB.create(test.MxsMongo, "compare");

    const cars = load_cars();

    await compare_insert_batch(mongo, nosql, cars);
    console.log();
    await compare_insert_individual(mongo, nosql, cars, 2000);

    nosql.close();
    mongo.close();
}

run();
