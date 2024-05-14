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

const assert = require('assert');
const test = require('./nosqltest');
const error = test.error;

const name = "cache";

describe(name, function () {
    this.timeout(test.timeout);

    let nosql_nocache;
    let nosql_cache;

    /*
     * HELPERS
     */

    /*
     * MOCHA
     */
    before(async function () {
        nosql_nocache = await test.NoSQL.create("nosql", test.config.nosql_port);
        nosql_cache = await test.NoSQL.create("nosql", test.config.nosql_cache_port);

        await nosql_nocache.deleteAll("cache");
        await nosql_nocache.insert_n("cache", 1);
    });

    it('Can fetch with cache.', async function () {
        var rv = await nosql_cache.find("cache");
        var docs = rv.cursor.firstBatch;

        assert.equal(docs.length, 1);
    });

    it('Can fetch without cache.', async function () {
        var rv = await nosql_nocache.find("cache");
        var docs = rv.cursor.firstBatch;

        assert.equal(docs.length, 1);
    });

    it('Cache uses cache', async function () {
        var rv;
        var docs;
        var count;

        rv = await nosql_nocache.find("cache");
        docs = rv.cursor.firstBatch;
        count = docs.length;

        await nosql_nocache.insert_n("cache", 1);

        rv = await nosql_nocache.find("cache");
        docs = rv.cursor.firstBatch;

        // After 1 insert, there should be one additional document...
        assert.equal(docs.length, count + 1);

        rv = await nosql_cache.find("cache");
        docs = rv.cursor.firstBatch;

        // ... but not if we are fetching via the caching protocol instance.
        assert.equal(docs.length, count);
    });

    it('Can invalidate.', async function () {
        // Reset
        await nosql_nocache.deleteAll("cache");
        await nosql_nocache.insert_n("cache", 1);

        var rv;
        var docs;

        // Fetch via caching and no-caching protocol instances and check
        // they have the same world-view.
        rv = await nosql_nocache.find("cache");
        docs = rv.cursor.firstBatch;
        var nocache_count = docs.length;

        assert.equal(nocache_count, 1);

        rv = await nosql_cache.find("cache");
        docs = rv.cursor.firstBatch;
        var cache_count = docs.length;

        assert.equal(nocache_count, cache_count);

        // Insert via the no-caching instance and check that the
        // worldview diverges.

        await nosql_nocache.insert_n("cache", 1);

        rv = await nosql_cache.find("cache");
        docs = rv.cursor.firstBatch;
        cache_count = docs.length;

        rv = await nosql_nocache.find("cache");
        docs = rv.cursor.firstBatch;
        nocache_count = docs.length;

        assert.equal(nocache_count, cache_count + 1);

        // Insert via the caching protocol instance and check
        // that it invalidates the cache.
        await nosql_cache.insert_n("cache", 1);

        rv = await nosql_cache.find("cache");
        docs = rv.cursor.firstBatch;
        cache_count = docs.length;

        rv = await nosql_nocache.find("cache");
        docs = rv.cursor.firstBatch;
        nocache_count = docs.length;

        assert.equal(cache_count, nocache_count);
    });

    after(function () {
        if (nosql_nocache) {
            nosql_nocache.close();
        }

        if (nosql_cache) {
            nosql_cache.close();
        }
    });
});
