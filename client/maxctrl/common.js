/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

var _ = require('lodash-getpath')
var request = require('request');
var colors = require('colors/safe');
var Table = require('cli-table');
var assert = require('assert')

module.exports = function() {

    // Common options for all commands
    this.program = require('yargs');

    // Request a resource collection and format it as a table
    this.getCollection = function (resource, fields) {

        doRequest(resource, function(res) {

            var header = []

            fields.forEach(function(i) {
                header.push(Object.keys(i))
            })

            var table = getTable(header)

            res.data.forEach(function(i) {
                row = []

                fields.forEach(function(p) {
                    var v = _.getPath(i, p[Object.keys(p)[0]], '')

                    if (Array.isArray(v)) {
                        v = v.join(', ')
                    }
                    row.push(v)
                })

                table.push(row)
            })

            console.log(table.toString())
        })
    }

    // Request a single resource and format it as a key-value list
    this.getResource = function (resource, fields) {

        doRequest(resource, function(res) {
            var table = getList()

            fields.forEach(function(i) {
                var k = Object.keys(i)[0]
                var path = i[k]
                var v = _.getPath(res.data, path, '')

                if (Array.isArray(v) && typeof(v[0]) != 'object') {
                    v = v.join(', ')
                } else if (typeof(v) == 'object') {
                    v = JSON.stringify(v, null, 4)
                }

                var o = {}
                o[k] = v
                table.push(o)
            })

            console.log(table.toString())
        })
    }

    // Helper for converting endpoints to acutal URLs
    this.getUri = function(endpoint, options) {
        var base = 'http://';
        var argv = this.program.argv

        if (argv.secure) {
            base = 'https://';
        }

        return base + argv.user + ':' + argv.password + '@' +
            argv.host + ':' + argv.port + '/v1/' + endpoint;
    }

    // Helper for executing requests and handling their responses
    this.doRequest = function(resource, cb, obj) {

        args = obj || {}
        args.uri = getUri(resource),
        args.json = true

        request(args, function(err, resp, res) {
            if (err) {
                // Failed to request
                console.log('Error:', JSON.stringify(err, null, 4))
            } else if (resp.statusCode == 200 && cb) {
                // Request OK, returns data
                cb(res)
            } else if (resp.statusCode == 204) {
                // Request OK, no data
                console.log(colors.green('OK'))
            } else {
                // Unexpected return code, probably an error
                console.log('Error:', resp.statusCode, resp.statusMessage)
                if (res) {
                    console.log(res)
                }
            }
        })
    }
}

function getList() {
    return new Table({ style: { head: ['cyan'] } })
}

// Creates a table-like array for output. The parameter is an array of header names
function getTable(headobj) {

    for (i = 0; i < headobj.length; i++) {
        headobj[i] = colors.cyan(headobj[i])
    }

    return new Table({
        head: headobj
    })
}
