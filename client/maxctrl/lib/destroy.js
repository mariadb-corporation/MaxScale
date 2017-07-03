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
require('../common.js')()

exports.command = 'destroy <command>'
exports.desc = 'Destroy objects'
exports.handler = function() {}
exports.builder = function(yargs) {
    yargs
        .command('server <name>', 'Destroy an unused server', {}, function(argv) {
            doRequest('servers/' + argv.name, null, {method: 'DELETE'})
        })
        .command('monitor <name>', 'Destroy an unused monitor', {}, function(argv) {
            doRequest('monitors/' + argv.name, null, {method: 'DELETE'})
        })
        .command('listener <service> <name>', 'Destroy an unused listener', {}, function(argv) {
            doRequest('services/' + argv.service + '/listeners/' + argv.name, null, {method: 'DELETE'})
        })
        .usage('Usage: destroy <command>')
        .help()
        .command('*', 'the default command', {}, () => {
            console.log('Unknown command. See output of `help destroy` for a list of commands.')
        })
}
