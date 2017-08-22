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

exports.command = 'stop <command>'
exports.desc = 'Stop objects'
exports.handler = function() {}
exports.builder = function(yargs) {
    yargs
        .command('service <name>', 'Stop a service', {}, function(argv) {
            maxctrl(argv, function(host) {
                return doRequest(host, 'services/' + argv.name + '/stop', null, {method: 'PUT'})
            })
        })
        .command('monitor <name>', 'Stop a monitor', {}, function(argv) {
            maxctrl(argv, function(host) {
                return doRequest(host, 'monitors/' + argv.name + '/stop', null, {method: 'PUT'})
            })
        })
        .command('maxscale', 'Stop MaxScale by stopping all services', {}, function(argv) {
            maxctrl(argv, function(host) {
                return doRequest(host, 'services/', function(res) {
                    var promises = []

                    res.data.forEach(function(i) {
                        promises.push(doRequest(host, 'services/' + i.id + '/stop', null, {method: 'PUT'}))
                    })

                    return Promise.all(promises)
                        .then(() => OK())
                })
            })
        })
        .usage('Usage: stop <command>')
        .help()
        .command('*', 'the default command', {}, function(argv) {
            maxctrl(argv, function(host) {
                return error('Unknown command. See output of `help stop` for a list of commands.')
            })
        })
}
