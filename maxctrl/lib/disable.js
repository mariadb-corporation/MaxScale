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

const log_levels = [
    'debug',
    'info',
    'notice',
    'warning'
]

exports.command = 'disable <command>'
exports.desc = 'Disable functionality'
exports.handler = function() {}
exports.builder = function(yargs) {
    yargs
        .command('log-priority <log>', 'Disable log priority [warning|notice|info|debug]', {}, function(argv) {
            if (log_levels.indexOf(argv.log) != -1) {
                maxctrl(argv, function(host) {
                    return updateValue(host, 'maxscale/logs', 'data.attributes.parameters.log_' + argv.log, false)
                })
            } else {
                maxctrl(argv, function() {
                    error('Invalid log priority: ' + argv.log)
                    return Promise.reject()
                })
            }
        })
        .command('maxlog', 'Disable MaxScale logging', {}, function(argv) {
            maxctrl(argv, function(host) {
                return updateValue(host, 'maxscale/logs', 'data.attributes.parameters.maxlog', false)
            })
        })
        .command('syslog', 'Disable syslog logging', {}, function(argv) {
            maxctrl(argv, function(host) {
                return updateValue(host, 'maxscale/logs', 'data.attributes.parameters.syslog', false)
            })
        })
        .command('account <name>', 'Disable a Linux user account from administrative use', {}, function(argv) {
            maxctrl(argv, function(host) {
                return doRequest(host, 'users/unix/' + argv.name, null, { method: 'DELETE'})
            })
        })
        .usage('Usage: disable <command>')
        .help()
        .command('*', 'the default command', {}, () => {
            logger.log('Unknown command. See output of `help disable` for a list of commands.')
        })
}
