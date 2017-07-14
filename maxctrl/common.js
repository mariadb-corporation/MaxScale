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

var request = require('request-promise-native');
var colors = require('colors/safe');
var Table = require('cli-table');
var consoleLib = require('console')
var fs = require('fs')

module.exports = function() {

    this._ = require('lodash-getpath')

    this.logger = console

    this.maxctrl = function(argv) {

        if (argv.quiet) {
            this.logger = new consoleLib.Console(fs.createWriteStream('/dev/null'),
                                                 fs.createWriteStream('/dev/null'))
        }

        this.argv = argv
        return this
    }

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

            logger.log(table.toString())
        })
    }

    // Request a part of a resource as a collection
    this.getSubCollection = function (resource, subres, fields) {

        doRequest(resource, function(res) {

            var header = []

            fields.forEach(function(i) {
                header.push(Object.keys(i))
            })

            var table = getTable(header)

            _.getPath(res.data, subres, []).forEach(function(i) {
                row = []

                fields.forEach(function(p) {
                    var v = _.getPath(i, p[Object.keys(p)[0]], '')

                    if (Array.isArray(v) && typeof(v[0]) != 'object') {
                        v = v.join(', ')
                    } else if (typeof(v) == 'object') {
                        v = JSON.stringify(v, null, 4)
                    }
                    row.push(v)
                })

                table.push(row)
            })

            logger.log(table.toString())
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

            logger.log(table.toString())
        })
    }

    // Helper for converting endpoints to acutal URLs
    this.getUri = function(host, secure, endpoint) {
        var base = 'http://'

        if (secure) {
            base = 'https://'
        }

        return base + argv.user + ':' + argv.password + '@' + host + '/v1/' + endpoint
    }

    // Helper for executing requests and handling their responses, returns a
    // promise that is fulfilled when all requests successfully complete. The
    // promise is rejected if any of the requests fails.
    this.doAsyncRequest = function(resource, cb, obj) {
        return new Promise(function (resolve, reject) {
            pingCluster(this.argv.hosts)
                .then(function() {

                    var promises = []

                    this.argv.hosts.forEach(function(host) {
                        args = obj || {}
                        args.uri = getUri(host, this.argv.secure, resource)
                        args.json = true
                        args.timeout = this.argv.timeout

                        var p = request(args)
                            .then(function(res) {
                                if (res && cb) {
                                    // Request OK, returns data
                                    if (!this.argv.tsv && this.argv.hosts.length > 1) {
                                        logger.log(colors.yellow(host) + ':')
                                    }

                                    return cb(res)
                                } else {
                                    // Request OK, no data or data is ignored
                                    if (this.argv.hosts.length > 1) {
                                        logger.log(colors.yellow(host) + ': ' + colors.green('OK'))
                                    } else {
                                        logger.log(colors.green('OK'))
                                    }
                                    return Promise.resolve()
                                }
                            }, function(err) {
                                if (this.argv.hosts.length > 1) {
                                    logger.log(colors.yellow(host) + ':')
                                }
                                if (err.response.body) {
                                    logError(JSON.stringify(err.response.body, null, 4))
                                } else {
                                    logError('Server responded with ' + err.statusCode)
                                }
                                return Promise.reject()
                            })

                        // Push the request on to the stack
                        promises.push(p)
                    })

                    // Wait for all promises to resolve or reject
                    Promise.all(promises)
                        .then(resolve, reject)
                }, function(err) {
                    // One of the HTTP request pings to the cluster failed, log the error
                    logError(JSON.stringify(err.error, null, 4))
                    reject()
                })
        })
    }

    this.doRequest = function(resource, cb, obj) {
        return doAsyncRequest(resource, cb, obj)
            .then(this.argv.resolve, this.argv.reject)
    }

    this.updateValue = function(resource, key, value) {
        var body = {}
        _.set(body, key, value)
        doRequest(resource, null, { method: 'PATCH', body: body })
    }

    this.logError = function(err) {
        this.logger.error(colors.red('Error:'), err)
    }

    this.error = function(err) {
        logger.log(colors.red('Error:'), err)
        this.argv.reject()
    }
}

var tsvopts = {
    chars: {
        'top': '' , 'top-mid': '' , 'top-left': '' , 'top-right': ''
        , 'bottom': '' , 'bottom-mid': '' , 'bottom-left': '' , 'bottom-right': ''
        , 'left': '' , 'left-mid': '' , 'mid': '' , 'mid-mid': ''
        , 'right': '' , 'right-mid': '' , 'middle': '	'
    },
    style: {
        'padding-left': 0,
        'padding-right': 0,
        compact: true
    },


}

function getList() {
    var opts = {
        style: { head: ['cyan'] }
    }

    if (this.argv.tsv)
    {
        opts = _.assign(opts, tsvopts)
    }

    return new Table(opts)
}

// Creates a table-like array for output. The parameter is an array of header names
function getTable(headobj) {

    for (i = 0; i < headobj.length; i++) {
        headobj[i] = colors.cyan(headobj[i])
    }

    var opts

    if (this.argv.tsv)
    {
        opts = _.assign(opts, tsvopts)
    } else {
        opts = {
            head: headobj
        }
    }

    return new Table(opts)
}

function pingCluster(hosts) {
    var promises = []

    hosts.forEach(function(i) {
        promises.push(request('http://' + i + '/v1'))
    })

    return Promise.all(promises)
}
