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
var os = require('os')
var fs = require('fs')
var readlineSync = require('readline-sync')

function normalizeWhitespace(table) {
    table.forEach((v) => {
        if (Array.isArray(v)) {
            // `table` is an array of arrays
            v.forEach((k) => {
                if (typeof(v[k]) == 'string') {
                    v[k] = v[k].replace( /\s+/g, ' ')
                }
            })
        } else if (!Array.isArray(v) && v instanceof Object) {
            // `table` is an array of objects
            Object.keys(v).forEach((k) => {
                if (typeof(v[k]) == 'string') {
                    v[k] = v[k].replace( /\s+/g, ' ')
                }
            })
        }
    })
}

module.exports = function() {

    this._ = require('lodash-getpath')

    // The main entry point into the library. This function is used to do
    // cluster health checks and to propagate the commands to multiple
    // servers.
    this.maxctrl = function(argv, cb) {

        // No password given, ask it from the command line
        if (argv.p == '') {
            argv.p = readlineSync.question('Enter password: ', {
                hideEchoBack: true
            })
        }

        // Split the hostnames, separated by commas
        argv.hosts = argv.hosts.split(',')

        this.argv = argv

        if (!argv.hosts || argv.hosts.length < 1) {
            argv.reject("No hosts defined")
        }

        return pingCluster(argv.hosts)
            .then(function() {
                var promises = []
                var rval = []

                argv.hosts.forEach(function(i) {
                    promises.push(cb(i)
                                  .then(function(output) {
                                      if (argv.hosts.length > 1) {
                                          rval.push(colors.yellow(i))
                                      }
                                      rval.push(output)
                                  }))
                })

                return Promise.all(promises)
                    .then(function() {
                        argv.resolve(argv.quiet ? undefined : rval.join(os.EOL))
                    }, function(err) {
                        argv.reject(err)
                    })
            }, function(err) {

                if (err.error.cert) {
                    // TLS errors cause extremely verbose errors, truncate the certifiate details
                    // from the error output
                    delete err.error.cert
                }

                // One of the HTTP request pings to the cluster failed, log the error
                argv.reject(JSON.stringify(err.error, null, 4))
            })
    }

    this.filterResource = function (res, fields) {
        table = []

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

        return table
    }

    this.tableToString = function(table) {

        if (this.argv.tsv)
        {
            // Convert whitespace into spaces to prevent breaking the TSV format
            normalizeWhitespace(table)
        }

        str = table.toString()

        if (this.argv.tsv) {
            // Based on the regex found in: https://github.com/jonschlinkert/strip-color
            str = str.replace( /\x1B\[[(?);]{0,2}(;?\d)*./g, '')
        }
        return str
    }

    // Get a resource as raw collection; a matrix of strings
    this.getRawCollection = function (host, resource, fields) {
        return getJson(host, resource)
            .then((res) => filterResource(res, fields))
    }

    this.rawCollectionAsTable = function (arr, fields) {
        var header = []

        fields.forEach(function(i) {
            header.push(Object.keys(i))
        })

        var table = getTable(header)

        arr.forEach((row) => {
            table.push(row)
        })
        return tableToString(table)
    }

    // Request a resource collection and format it as a table
    this.getCollection = function (host, resource, fields) {
        return getRawCollection(host, resource, fields)
            .then((res) => rawCollectionAsTable(res, fields))
    }

    // Request a part of a resource as a collection
    this.getSubCollection = function (host, resource, subres, fields) {

        return doRequest(host, resource, function(res) {
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

            return tableToString(table)
        })
    }

    // Request a single resource and format it as a key-value list
    this.getResource = function (host, resource, fields) {

        return doRequest(host, resource, function(res) {
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

            return tableToString(table)
        })
    }

    this.updateValue = function(host, resource, key, value) {
        var body = {}

        // Convert string booleans into JSON booleans
        if (value == "true") {
            value = true
        } else if (value == "false") {
            value = false
        }

        _.set(body, key, value)
        return doRequest(host, resource, null, { method: 'PATCH', body: body })
    }

    // Helper for converting endpoints to acutal URLs
    this.getUri = function(host, secure, endpoint) {
        var base = 'http://'

        if (secure) {
            base = 'https://'
        }

        return base + host + '/v1/' + endpoint
    }

    this.OK = function() {
        return Promise.resolve(colors.green('OK'))
    }


    this.setTlsCerts = function(args) {
        args.agentOptions = {}
        if (this.argv['tls-key']) {
            args.agentOptions.key = fs.readFileSync(this.argv['tls-key'])
        }

        if (this.argv['tls-cert']) {
            args.agentOptions.cert = fs.readFileSync(this.argv['tls-cert'])
        }

        if (this.argv['tls-ca-cert']) {
            args.agentOptions.ca = fs.readFileSync(this.argv['tls-ca-cert'])
        }

        if (this.argv['tls-passphrase']) {
            args.agentOptions.passphrase = this.argv['tls-passphrase']
        }

        if (!this.argv['tls-verify-server-cert']) {
            args.agentOptions.checkServerIdentity = function() {
            }
        }
    }

    // Helper for executing requests and handling their responses, returns a
    // promise that is fulfilled when all requests successfully complete. The
    // promise is rejected if any of the requests fails.
    this.doAsyncRequest = function(host, resource, cb, obj) {
        args = obj || {}
        args.uri = getUri(host, this.argv.secure, resource)
        args.auth = {user: argv.u, pass: argv.p}
        args.json = true
        args.timeout = this.argv.timeout
        setTlsCerts(args)

        return request(args)
            .then(function(res) {
                if (res && cb) {
                    // Request OK, returns data
                    return cb(res)
                } else {
                    // Request OK, no data or data is ignored
                    return OK()
                }
            }, function(err) {
                if (err.response && err.response.body) {
                    return error('Server at '+ err.response.request.uri.host +' responded with status code ' + err.statusCode + ' to `' + err.response.request.method +' ' + resource + '`:' + JSON.stringify(err.response.body, null, 4))
                } else if (err.statusCode) {
                    return error('Server at '+ err.response.request.uri.host +' responded with status code ' + err.statusCode + ' to `' + err.response.request.method +' ' + resource + '`')
                } else if (err.error) {
                    return error(JSON.stringify(err.error, null, 4))
                } else {
                    return error('Undefined error: ' + JSON.stringify(err, null, 4))
                }
            })
    }

    this.doRequest = function(host, resource, cb, obj) {
        return doAsyncRequest(host, resource, cb, obj)
    }

    this.getJson = function(host, resource) {
        return doAsyncRequest(host, resource, (res) => {
            return res
        })
    }

    this.error = function(err) {
        return Promise.reject(colors.red('Error: ') +  err)
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

    if (hosts.length > 1 ) {
        hosts.forEach(function(i) {
            args = {}
            args.uri = getUri(i, this.argv.secure, '')
            args.json = true
            setTlsCerts(args)
            promises.push(request(args))
        })
    }

    return Promise.all(promises)
}
