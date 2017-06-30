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

exports.command = 'clear <command>'
exports.desc = 'Clear object state'
exports.handler = function() {}
exports.builder = function(yargs) {
    yargs
        .command('server <server> <state>', 'Clear server state', {}, function(argv) {
            var target = 'servers/' + argv.server + '/clear?state=' + argv.state
            doRequest(target, null, {method: 'POST'})
        })
        .usage('Usage: clear <command>')
        .help()
        .command('*', 'the default command', {}, () => {
            console.log('Unknown command. See output of `help clear` for a list of commands.')
        })
}
