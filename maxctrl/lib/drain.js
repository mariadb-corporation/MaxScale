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
require('./common.js')()

function waitUntilZero(host, target, path, timeout) {
    return new Promise((resolve, reject) => {
        const interval = 2 // How often we poll the value
        var total = 0 // Estimation of how long we've been waiting

        // Using a timer will slow down the initial check but given that the idea is to drain the
        // node, it shouldn't be that problematic
        var timer = setInterval(() => {
            total += interval

            // Read and extract the value

            // Note: It is possible that the interval between requests is less than the configured 2
            // seconds since doing the request itself takes some time. This means that it's also
            // possible that parallel requests are executed which could cause problems. Upgrading to
            // a newer Node.js would allow the use of async/await which should make these sorts of
            // things easier to deal with.
            doRequest(host, target, (res) => {
                var v = _.get(res, path, -1)

                if (v <= 0 || total >= timeout) {
                    // Value is zero or the timeout was hit
                    clearInterval(timer)

                    if (v == -1) {
                        // This should never happen as long as correct versions are used
                        reject('Invalid path: ' + path)
                    } else if (total >= timeout) {
                        reject('Drain timeout exceeded')
                    } else {
                        resolve()
                    }
                }
            })
            .catch((err) => reject(err))

        }, 2000)
    })
}

exports.command = 'drain <command>'
exports.desc = 'Drain objects'
exports.handler = function() {}
exports.builder = function(yargs) {
    yargs
        .group(['drain-timeout'], 'Drain options:')
        .option('drain-timeout', {
            describe: 'Timeout for the drain operation in seconds. If exceeded, the server ' +
                'is added back to all services without putting it into maintenance mode.',
            default: 90,
            type: 'number'
        })
        .command('server <server>', 'Drain a server of connections', function(yargs) {
            return yargs.epilog('This command drains the server of connections by first removing it ' +
                                'from all services after which it waits until all connections are ' +
                                'closed. When all connections are closed, the server is put into the ' +
                                '`maintenance` state and added back to all the services where it was ' +
                                'removed from. To take the server back into use, execute ' +
                                '`clear server <server> maintenance`.')
                .usage('Usage: drain server <server>')
        }, function(argv) {

            maxctrl(argv, function(host) {

                var target = 'servers/' + argv.server
                var path = 'data.relationships.services.data'
                var timeout = argv['drain-timeout']

                return doRequest(host, target, (res) => {
                    // Store the services, used later to add the server back into them
                    var services =_.get(res, path, [])

                    // Remove the relationships
                    _.set(res, path, [])

                    // Remove unneeded data
                    delete res.data.attributes

                    var addServersBack = () => {
                        _.set(res, path, services)
                        return doRequest(host, target, null, {method: 'PATCH', body: res})
                    }

                    return doRequest(host, target, null, {method: 'PATCH', body: res})
                        .then(() => waitUntilZero(host, target, 'data.attributes.statistics.connections', timeout))
                        .then(() => doRequest(host, target + '/set?state=maintenance', null, {method: 'PUT'}))
                        .then(addServersBack, addServersBack) // Try to add the servers back even if we receive an error
                })
            })
        })
        .usage('Usage: drain <command>')
        .help()
        .command('*', 'the default command', {}, function(argv) {
            maxctrl(argv, function(host) {
                return error('Unknown command. See output of `help drain` for a list of commands.')
            })
        })
}
