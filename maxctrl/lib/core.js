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

program
    .version(maxctrl_version)
    .strict()
    .exitProcess(false)
    .showHelpOnFail(false)
    .group(['u', 'p', 'h', 't', 'q', 'tsv'], 'Global Options:')
    .option('u', {
        alias:'user',
        global: true,
        default: 'admin',
        describe: 'Username to use',
        type: 'string'
    })
    .option('p', {
        alias: 'password',
        describe: 'Password for the user. To input the password manually, give -p as the last argument or use --password=\'\'',
        default: 'mariadb',
        type: 'string'
    })
    .option('h', {
        alias: 'hosts',
        describe: 'List of MaxScale hosts. The hosts must be in ' +
            'HOST:PORT format and each value must be separated by a comma.',
        default: 'localhost:8989',
        type: 'string'
    })
    .option('t', {
        alias: 'timeout',
        describe: 'Request timeout in milliseconds',
        default: 10000,
        type: 'number'
    })
    .option('q', {
        alias: 'quiet',
        describe: 'Silence all output',
        default: false,
        type: 'boolean'
    })
    .option('tsv', {
        describe: 'Print tab separated output',
        default: false,
        type: 'boolean'
    })
    .group(['s', 'tls-key', 'tls-cert', 'tls-ca-cert', 'tls-verify-server-cert'], 'HTTPS/TLS Options:')
    .option('s', {
        alias: 'secure',
        describe: 'Enable HTTPS requests',
        default: false,
        type: 'boolean'
    })
    .option('tls-key', {
        describe: 'Path to TLS private key',
        type: 'string'
    })
    .option('tls-cert', {
        describe: 'Path to TLS public certificate',
        type: 'string'
    })
    .option('tls-passphrase', {
        describe: 'Password for the TLS private key',
        type: 'string'
    })
    .option('tls-ca-cert', {
        describe: 'Path to TLS CA certificate',
        type: 'string'
    })
    .option('tls-verify-server-cert', {
        describe: 'Whether to verify server TLS certificates',
        default: true,
        type: 'boolean'
    })

    .command(require('./list.js'))
    .command(require('./show.js'))
    .command(require('./set.js'))
    .command(require('./clear.js'))
    .command(require('./drain.js'))
    .command(require('./enable.js'))
    .command(require('./disable.js'))
    .command(require('./create.js'))
    .command(require('./destroy.js'))
    .command(require('./link.js'))
    .command(require('./unlink.js'))
    .command(require('./start.js'))
    .command(require('./stop.js'))
    .command(require('./alter.js'))
    .command(require('./rotate.js'))
    .command(require('./call.js'))
    .command(require('./cluster.js'))
    .command(require('./api.js'))
    .help()
    .demandCommand(1, 'At least one command is required')
    .command('*', 'the default command', {}, function(argv) {
        maxctrl(argv, function() {
            msg = argv._.length == 0 ? 'No command given' : 'Unknown command ' + JSON.stringify(argv._)
            return error(msg + '. See output of `help` for a list of commands.')
        })
    })

module.exports.execute = function(argv, opts) {
    if (opts && opts.extra_args) {
        // Add extra options to the end of the argument list
        argv = argv.concat(opts.extra_args)
    }

    return new Promise(function(resolve, reject) {
        program
            .parse(argv, {resolve: resolve, reject: reject}, function(err, argv, output) {
                if (err) {
                    reject(err.message)
                } else if (output) {
                    resolve(output)
                }
            })
    })
}
