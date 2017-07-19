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

exports.command = 'alter <command>'
exports.desc = 'Alter objects'
exports.handler = function() {}
exports.builder = function(yargs) {
    yargs
        .command('server <server> <key> <value>', 'Alter server parameters', {}, function(argv) {
            maxctrl(argv, function(host) {
                return updateValue(host, 'servers/' + argv.server, 'data.attributes.parameters.' + argv.key, argv.value)
            })
        })
        .command('monitor <monitor> <key> <value>', 'Alter monitor parameters', {}, function(argv) {
            maxctrl(argv, function(host) {
                return updateValue(host, 'monitors/' + argv.monitor, 'data.attributes.parameters.' + argv.key, argv.value)
            })
        })
        .command('service <service> <key> <value>', 'Alter service parameters', {}, function(argv) {
            maxctrl(argv, function(host) {
                return updateValue(host, 'services/' + argv.service, 'data.attributes.parameters.' + argv.key, argv.value)
            })
        })
        .command('logging <key> <value>', 'Alter logging parameters', {}, function(argv) {
            maxctrl(argv, function(host) {
                return updateValue(host, 'maxscale/logs', 'attributes.parameters.' + argv.key, argv.value)
            })
        })
        .command('maxscale <key> <value>', 'Alter MaxScale parameters', {}, function(argv) {
            maxctrl(argv, function(host) {
                return updateValue(host, 'maxscale', 'attributes.parameters.' + argv.key, argv.value)
            })
        })
        .usage('Usage: alter <command>')
        .help()
        .command('*', 'the default command', {}, () => {
            logger.log('Unknown command. See output of `help alter` for a list of commands.')
        })
}
