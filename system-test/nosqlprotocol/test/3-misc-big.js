/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

const assert = require('assert');
const test = require('./nosqltest');
const error = test.error;

const name = "big";

describe(name, function () {
    this.timeout(120 * 1000);

    let nosql;

    /*
     * HELPERS
     */
    async function delete_all()
    {
        var command = {
            delete: name,
            deletes: [{q:{}, limit:0}]
        };
        await nosql.runCommand(command);
    }

    /*
     * MOCHA
     */
    before(async function () {
        nosql = await test.NoSQL.create();
    });

    it('Can insert max size document.', async function () {
        delete_all();

        var documents = [];

        var doc = {
            s: new Array(16776971).join("a")
        }

        documents.push(doc);

        var command = {
            insert: "big",
            documents: documents
        };

        var rv = await nosql.ntRunCommand(command);

        assert.equal(rv.ok, 1);
        assert.equal(rv.$err, undefined);
    });

    it('Can not insert too big a document.', async function () {
        delete_all();

        var documents = [];

        var doc = {
            s: new Array(16777216).join("a")
        }

        documents.push(doc);

        var command = {
            insert: "big",
            documents: documents
        };

        var rv = await nosql.ntRunCommand(command);

        assert.equal(rv.ok, 1);
        assert.equal(rv.writeErrors[0].code, error.BAD_VALUE);
    });

    it('Can insert many big documents.', async function () {
        delete_all();

        // According to the documentation, the size limit of each document
        // should be 16MB, but it seems to apply to the command document
        // as well, so the total size of the documents must be < 16MB.

        var documents = [];

        for (var i = 0; i < 16; ++i) {
            var doc = {
                s: new Array(1000000).join("a")
            }

            documents.push(doc);
        }

        var command = {
            insert: "big",
            documents: documents
        };

        var rv = await nosql.ntRunCommand(command);

        assert.equal(rv.ok, 1);
        assert.equal(rv.$err, undefined);
    });

    it('Can fetch many big documents.', async function () {
        delete_all();

        var original_total = 0;
        var original_count = 0;

        for (var j = 0; j < 7; ++j) {
            // Add 1 max size.
            var documents = [];

            var doc = {
                s: new Array(16776971).join("a")
            }

            original_total += doc.s.length;

            documents.push(doc);
            ++original_count;

            var command = {
                insert: "big",
                documents: documents
            };

            await nosql.runCommand(command);

            // Add many biggish
            var documents = [];

            for (var i = 0; i < 16; ++i) {
                var doc = {
                    s: new Array(1000000).join("a")
                }

                original_total += doc.s.length;

                documents.push(doc);
                ++original_count;
            }

            var command = {
                insert: "big",
                documents: documents
            };

            await nosql.runCommand(command);
        }

        // Then fetch them all.
        var command = {
            find: name
        };

        var rv = await nosql.runCommand(command);

        var fetched_total = 0;
        var fetched_count = 0;

        for (var doc of rv.cursor.firstBatch) {
            fetched_total += doc.s.length;
            ++fetched_count;
        }

        var id = rv.cursor.id;

        while (id) {
            command = {
                getMore: id,
                collection: name
            };

            var rv = await nosql.runCommand(command);

            for (var doc of rv.cursor.nextBatch) {
                fetched_total += doc.s.length;
                ++fetched_count;
            }

            id = rv.cursor.id;
        }

        assert.equal(fetched_count, original_count);
        assert.equal(fetched_total, original_total);
    });

    after(function () {
        if (nosql) {
            nosql.close();
        }
    });
});
