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

exports.command = 'set <command>'
exports.desc = 'Set object status'
exports.handler = function() {}
exports.builder = function(yargs) {
    yargs
        .command('server <server> <status>', 'Set server status', {}, function(argv) {
            var target = 'servers/' + argv.server + '/set?status=' + argv.status
            doRequest(target, null, {method: 'POST'})
        })
        .usage('Usage: set <command>')
        .help()
        .command('*', 'the default command', {}, () => {
            console.log('Unknown command. See output of `help set` for a list of commands.')
        })
}
