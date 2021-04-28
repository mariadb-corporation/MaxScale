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

if (!process.env.maxscale_000_network) {
    console.log("The environment variable 'maxscale_000_network' must be set.");
    process.exit(1);
}

var config = {
    host: process.env.maxscale_000_network,
    mariadb_port: 4008,
    mxsmongodb_port: 4711,
    mngmongodb_port: 27017,
    user: 'maxskysql',
    password: 'skysql'
};

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
    },

    resetTable: async function (conn) {
        await conn.query("DROP TABLE IF EXISTS test.mongo");
        await conn.query("CREATE TABLE test.mongo (id TEXT NOT NULL UNIQUE, doc JSON)");
    }
}

var MxsMongo = {
    createClient: async function () {
        var uri = "mongodb://" + config.host + ":" + config.mxsmongodb_port;
        client = new mongodb.MongoClient(uri, { useUnifiedTopology: true });
        await client.connect();
        return client;
    }
};

var MngMongo = {
    createClient: async function () {
        var uri = "mongodb://" + config.host + ":" + config.mngmongodb_port;
        client = new mongodb.MongoClient(uri, { useUnifiedTopology: true });
        await client.connect();
        return client;
    }
};

class MDB {
    constructor(client, db) {
        this.client = client;
        this.db = db;
    }

    static async create(m, name) {
        var client = await m.createClient();

        if (!name) {
            name = "test";
        }

        var db = client.db(name);

        return new MDB(client, db);
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

    async runCommand(command) {
        return await this.db.command(command);
    }

    async nothrowCommand(command) {
        var rv;
        try {
            rv = await this.db.command(command);
        }
        catch (x) {
            rv = x;
        }

        return rv;
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
    MDB
};
