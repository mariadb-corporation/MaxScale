/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
require('./common.js')()

exports.command = 'destroy <command>'
exports.desc = 'Destroy objects'
exports.handler = function() {}
exports.builder = function(yargs) {
    yargs
        .command('server <name>', 'Destroy an unused server', function(yargs) {
            return yargs.epilog('The server must be unlinked from all services and monitor before it can be destroyed.')
                .usage('Usage: destroy server <name>')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return doRequest(host, 'servers/' + argv.name, null, {method: 'DELETE'})
            })
        })
        .command('monitor <name>', 'Destroy an unused monitor', function(yargs) {
            return yargs.epilog('The monitor must be unlinked from all servers before it can be destroyed.')
                .usage('Usage: destroy monitor <name>')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return doRequest(host, 'monitors/' + argv.name, null, {method: 'DELETE'})
            })
        })
        .command('listener <service> <name>', 'Destroy an unused listener', function(yargs) {
            return yargs.epilog('Destroying a monitor causes it to be removed on the next restart. ' +
                                'Destroying a listener at runtime stops it from accepting new ' +
                                'connections but it will still be bound to the listening socket. This ' +
                                'means that new listeners cannot be created to replace destroyed listeners ' +
                                'without restarting MaxScale.')
                .usage('Usage: destroy listener <service> <name>')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return doRequest(host, 'services/' + argv.service + '/listeners/' + argv.name, null, {method: 'DELETE'})
            })
        })
        .command('service <name>', 'Destroy an unused service', function(yargs) {
            return yargs.epilog('The service must be unlinked from all servers and filters. ' +
                                'All listeners for the service must be destroyed before the service ' +
                                'itself can be destroyed.')
                .usage('Usage: destroy service <name>')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return doRequest(host, 'services/' + argv.name, null, {method: 'DELETE'})
            })
        })
        .command('filter <name>', 'Destroy an unused filter', function(yargs) {
            return yargs.epilog('The filter must not be used by any service when it is destroyed.')
                .usage('Usage: destroy filter <name>')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return doRequest(host, 'filters/' + argv.name, null, {method: 'DELETE'})
            })
        })
        .command('user <name>', 'Remove a network user', function(yargs) {
            return yargs.epilog('The last remaining administrative user cannot be removed. ' +
                                'Create a replacement administrative user before attempting ' +
                                'to remove the last administrative user.')
                .usage('Usage: destroy user <name>')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return doRequest(host, 'users/inet/' + argv.name, null, {method: 'DELETE'})
            })
        })
        .usage('Usage: destroy <command>')
        .help()
        .command('*', 'the default command', {}, function(argv) {
            maxctrl(argv, function(host) {
                return error('Unknown command. See output of `help destroy` for a list of commands.')
            })
        })
}
