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

var fs = require('fs')
var program = require('yargs');

const maxctrl_version = '1.0.0';

require('./common.js')()

program
    .version(maxctrl_version)
    .group(['u', 'p', 'h', 's', 't', 'q', 'tsv'], 'Global Options:')
    .option('u', {
        alias:'user',
        global: true,
        default: 'admin',
        describe: 'Username to use',
        type: 'string'
    })
    .option('p', {
        alias: 'password',
        describe: 'Password for the user',
        default: 'mariadb',
        type: 'string'
    })
    .option('h', {
        alias: 'hosts',
        describe: 'List of MaxScale hosts. The hosts must be in ' +
            '<hostname>:<port> format and each host must be separated by spaces.',
        default: 'localhost:8989',
        type: 'array'
    })
    .option('s', {
        alias: 'secure',
        describe: 'Enable HTTPS requests',
        default: 'false',
        type: 'boolean'
    })
    .option('t', {
        alias: 'timeout',
        describe: 'Request timeout in milliseconds',
        default: '10000',
        type: 'number'
    })
    .option('q', {
        alias: 'quiet',
        describe: 'Silence all output',
        default: 'false',
        type: 'boolean'
    })
    .option('tsv', {
        describe: 'Print tab separated output',
        default: 'false',
        type: 'boolean'
    })

    .command(require('./lib/list.js'))
    .command(require('./lib/show.js'))
    .command(require('./lib/set.js'))
    .command(require('./lib/clear.js'))
    .command(require('./lib/enable.js'))
    .command(require('./lib/disable.js'))
    .command(require('./lib/create.js'))
    .command(require('./lib/destroy.js'))
    .command(require('./lib/link.js'))
    .command(require('./lib/unlink.js'))
    .command(require('./lib/start.js'))
    .command(require('./lib/stop.js'))
    .command(require('./lib/alter.js'))
    .command(require('./lib/rotate.js'))
    .command(require('./lib/call.js'))
    .help()
    .demandCommand(1, 'At least one command is required')
    .command('*', 'the default command', {}, () => {
        console.log('Unknown command. See output of `help` for a list of commands.')
    })

module.exports.execute = function(argv, opts) {
    if (opts && opts.extra_args) {
        // Add extra options to the end of the argument list
        argv = argv.concat(opts.extra_args)
    }

    return new Promise(function(resolve, reject) {
        program
            .parse(argv, {resolve: resolve, reject: reject})
    })
}
