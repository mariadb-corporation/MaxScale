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
        .command('service <name>', 'Start a service', {}, function(argv) {
            maxctrl(argv, function(host) {
                return doRequest(host, 'services/' + argv.name + '/start', null, {method: 'PUT'})
            })
        })
        .command('monitor <name>', 'Start a monitor', {}, function(argv) {
            maxctrl(argv, function(host) {
                return doRequest(host, 'monitors/' + argv.name + '/start', null, {method: 'PUT'})
            })
        })
        .usage('Usage: start <command>')
        .help()
        .command('*', 'the default command', {}, () => {
            logger.log('Unknown command. See output of `help start` for a list of commands.')
        })
}
