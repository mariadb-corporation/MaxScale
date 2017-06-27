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
    const maxctrl_version = '1.0.0';

    // Common options for all commands
    this.program = require('yargs');
    this.program
        .group(['u', 'p', 'h', 'p', 'P', 's'], 'Global Options:')
        .option('u', {
            alias:'user',
            global: true,
            default: 'mariadb',
            describe: 'Username to use',
            type: 'string'
        })
        .option('p', {
            alias: 'password',
            describe: 'Password for the user',
            default: 'admin',
            type: 'string'
        })
        .option('h', {
            alias: 'host',
            describe: 'The hostname or address where MaxScale is located',
            default: 'localhost',
            type: 'string'
        })
        .option('P', {
            alias: 'port',
            describe: 'The port where MaxScale REST API listens on',
            default: 8989,
            type: 'number'
        })
        .option('s', {
            alias: 'secure',
            describe: 'Enable TLS encryption of connections',
            default: 'false',
            type: 'boolean'
        })
        .version(maxctrl_version)
        .help()

    // Request a resource collection and format it as a table
    this.getCollection = function (resource, headers, parts) {

        doRequest(resource, function(res) {
            var table = getTable(headers)

            res.data.forEach(function(i) {
                row = []

                parts.forEach(function(p) {
                    var v = _.getPath(i, p, "")

                    if (Array.isArray(v)) {
                        v = v.join(", ")
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
            var table = new Table()

            fields.forEach(function(i) {
                var k = Object.keys(i)[0]
                var path = i[k]
                var v = _.getPath(res.data, path, "")

                if (Array.isArray(v) && typeof(v[0]) != 'object') {
                    v = v.join(", ")
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
    this.doRequest = function(resource, cb) {
        request({
            uri: getUri(resource),
            json: true
        }, function(err, resp, res) {
            if (resp.statusCode == 200) {
                cb(res)
            } else if (resp.statusCode == 204) {
                console.log(colors.green("OK"))
            } else {
                console.log("Error:", resp.statusCode, resp.statusMessage)
                if (res) {
                    console.log(res)
                }
            }
        })
    }
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
