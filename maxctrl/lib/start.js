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

exports.command = 'start <command>'
exports.desc = 'Start objects'
exports.handler = function() {}
exports.builder = function(yargs) {
    yargs
        .command('service <name>', 'Start a service', function(yargs) {
            return yargs.epilog('This starts a service stopped by `stop service <name>`')
                .usage('Usage: start service <name>')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return doRequest(host, 'services/' + argv.name + '/start', null, {method: 'PUT'})
            })
        })
        .command('monitor <name>', 'Start a monitor', function(yargs) {
            return yargs.epilog('This starts a monitor stopped by `stop monitor <name>`')
                .usage('Usage: start monitor <name>')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return doRequest(host, 'monitors/' + argv.name + '/start', null, {method: 'PUT'})
            })
        })
        .command('maxscale', 'Start MaxScale by starting all services', function(yargs) {
            return yargs.epilog('This command will execute the `start service` command for ' +
                                'all services in MaxScale.')
                .usage('Usage: start maxscale')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return doRequest(host, 'services/', function(res) {
                    var promises = []

                    res.data.forEach(function(i) {
                        promises.push(doRequest(host, 'services/' + i.id + '/start', null, {method: 'PUT'}))
                    })

                    return Promise.all(promises)
                        .then(() => OK())
                })
            })
        })
        .usage('Usage: start <command>')
        .help()
        .command('*', 'the default command', {}, function(argv) {
            maxctrl(argv, function(host) {
                return error('Unknown command. See output of `help start` for a list of commands.')
            })
        })
}
