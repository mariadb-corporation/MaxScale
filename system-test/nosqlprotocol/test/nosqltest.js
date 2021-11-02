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

var maxscale_host = process.env.maxscale_000_network;

if (!maxscale_host) {
    console.log("The environment variable 'maxscale_000_network' must be set.");
    process.exit(1);
}

var timeout;

if (maxscale_host == "127.0.0.1") {
    // We are debugging, so let's set the timeout to an hour.
    timeout = 60 * 60 * 1000;
}
else {
    // Otherwise 10 seconds. 2 seconds is the default, which is too little for some
    // of the tests.
    timeout = 10000;
}

var config = {
    host: process.env.maxscale_000_network,
    mariadb_port: 4008,
    nosql_port: 17017,
    mongodb_port: 27017,
    user: 'maxskysql',
    password: 'skysql'
};

var error = {
    BAD_VALUE: 2,
    NO_SUCH_KEY: 4,
    UNAUTHORIZED: 13,
    TYPE_MISMATCH: 14,
    COMMAND_FAILED: 125
};

const fs = require('fs');
const mariadb = require('mariadb');
const mongodb = require('mongodb')
const assert = require('assert');

var MariaDB = {
    createConnection: async function () {
        return mariadb.createConnection({
            host: config.host,
            port: config.mariadb_port,
            user: config.user,
            password: config.password });
    }
}

var MxsMongo = {
    createClient: async function () {
        var uri = "mongodb://" + config.host + ":" + config.nosql_port;
        client = new mongodb.MongoClient(uri, { useUnifiedTopology: true });
        await client.connect();
        return client;
    }
};

var MngMongo = {
    createClient: async function () {
        var uri = "mongodb://" + config.host + ":" + config.mongodb_port;
        client = new mongodb.MongoClient(uri, { useUnifiedTopology: true });
        await client.connect();
        return client;
    }
};

class MDB {
    constructor(client, db) {
        this.client = client;
        this.db = db;
        this.admin = this.client.db('admin');
    }

    static async create(m, dbname) {
        var client = await m.createClient();

        if (!dbname) {
            dbname = "test";
        }

        var db = client.db(dbname);

        return new MDB(client, db);
    }

    async set_db(dbname) {
        this.db = this.client.db(dbname);
    }

    async close() {
        await this.client.close();
        this.client = null;
        this.db = null;
    }

    async reset(name) {
        try {
            await this.db.command({drop: name});
        }
        catch (x)
        {
            if (x.code != 26) // NameSpace not found
            {
                throw x;
            }
        }
    }

    async find(name, options) {
        var command = {
            find: name
        };

        if (options) {
            for (var p in options) {
                command[p] = options[p];
            }
        }

        return await this.runCommand(command);
    };

    async insert_n(name, n, cb) {
        var documents = [];
        for (var i = 0; i < n; ++i) {
            var doc = {};

            if (cb) {
                cb(doc);
            }
            else {
                doc.i = i;
            };

            documents.push(doc);
        }

        var command = {
            insert: name,
            documents: documents
        };

        return await this.runCommand(command);
    }

    async getLastError() {
        var rv;

        try {
            rv = await this.runCommand({"getLastError": 1});
        }
        catch (x)
        {
            rv = x;
        }

        return rv;
    }

    async deleteAll(name) {
        await this.db.command({delete: name, deletes: [{q: {}, limit: 0}]});
    }

    async runCommand(command) {
        return await this.db.command(command);
    }

    async adminCommand(command) {
        return await this.admin.command(command);
    }

    async ntRunCommand(command) {
        var rv;
        try {
            rv = await this.db.command(command);
        }
        catch (x) {
            rv = x;
        }

        return rv;
    }

    async ntAdminCommand(command) {
        var rv;
        try {
            rv = await this.admin.command(command);
        }
        catch (x) {
            rv = x;
        }

        return rv;
    }

    async delete_cars() {
        await this.deleteAll("cars");
    }

    async insert_cars() {
        const cars = this.db.collection("cars");

        var doc = JSON.parse(fs.readFileSync("test/cars.json", "utf8"));
        var docs = doc.cars;

        const options = { ordered: true };

        return await cars.insertMany(docs, options);
    }
};

module.exports = {
    config,
    mariadb,
    mongodb,
    assert,
    MariaDB,
    MxsMongo,
    MngMongo,
    MDB,
    error,
    timeout
};
