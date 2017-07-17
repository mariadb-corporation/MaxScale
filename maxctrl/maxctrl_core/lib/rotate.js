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

exports.command = 'rotate <command>'
exports.desc = 'Rotate log files'
exports.handler = function() {}
exports.builder = function(yargs) {
    yargs
        .command('logs', 'Rotate log files by closing and reopening the files', {}, function(argv) {
            maxctrl(argv, function(host){
                return doRequest(host, 'maxscale/logs/flush/', null, {method: 'POST'})
            })
        })
        .usage('Usage: rotate <command>')
        .help()
        .command('*', 'the default command', {}, () => {
            logger.log('Unknown command. See output of `help rotate` for a list of commands.')
        })
}
