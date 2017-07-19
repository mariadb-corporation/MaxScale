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

exports.command = 'enable <command>'
exports.desc = 'Enable functionality'
exports.handler = function() {}
exports.builder = function(yargs) {
    yargs
        .command('log-priority <log>', 'Enable log priority [warning|notice|info|debug]', {}, function(argv) {
            if (log_levels.indexOf(argv.log) != -1) {
                maxctrl(argv, function(host) {
                    return updateValue(host, 'maxscale/logs', 'data.attributes.parameters.log_' + argv.log, true)
                })
            } else {
                maxctrl(argv, function() {
                    error('Invalid log priority: ' + argv.log)
                    return Promise.reject()
                })
            }
        })
        .command('maxlog', 'Enable MaxScale logging', {}, function(argv) {
            maxctrl(argv, function(host) {
                return updateValue(host, 'maxscale/logs', 'data.attributes.parameters.maxlog', true)
            })
        })
        .command('syslog', 'Enable syslog logging', {}, function(argv) {
            maxctrl(argv, function(host) {
                return updateValue(host, 'maxscale/logs', 'data.attributes.parameters.syslog', true)
            })
        })
        .command('account <name>', 'Activate a Linux user account for administrative use', {}, function(argv) {
            var req_body = {
                data: {
                    id: argv.name,
                    type: 'unix'
                }
            }
            maxctrl(argv, function(host) {
                return doRequest(host, 'users/unix', null, { method: 'POST', body: req_body})
            })
        })
        .usage('Usage: enable <command>')
        .help()
        .command('*', 'the default command', {}, () => {
            logger.log('Unknown command. See output of `help enable` for a list of commands.')
        })
}
