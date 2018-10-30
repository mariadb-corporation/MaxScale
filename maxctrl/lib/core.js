/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

var fs = require('fs')
var program = require('yargs');
var inquirer = require('inquirer')

// Note: The version.js file is generated at configuation time. If you are
// building in-source, manually create the file
const maxctrl_version = require('./version.js').version;

// Global options given at startup
var base_opts = []

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
    .command(require('./classify.js'))
    .epilog('If no commands are given, maxctrl is started in interactive mode. ' +
            'Use `exit` to exit the interactive mode.')
    .help()
    .demandCommand(1, 'At least one command is required')
    .command('*', 'the default command', {}, function(argv) {
        if (argv._.length == 0) {
            base_opts = ['--user=' + argv.user,
                        '--password=' + argv.password,
                        '--hosts=' + argv.hosts,
                        '--timeout=' + argv.timeout]
            return askQuestion()
        } else {
            maxctrl(argv, function() {
                msg = 'Unknown command ' + JSON.stringify(argv._)
                return error(msg + '. See output of `help` for a list of commands.')
            })
        }
    })

function doCommand(argv) {
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

module.exports.execute = function(argv, opts) {
    if (opts && opts.extra_args) {
        // Add extra options to the end of the argument list
        argv = argv.concat(opts.extra_args)
    }

    return doCommand(argv)
}

function askQuestion() {
    var question = [ {
        name: 'maxctrl',
        prefix: '',
        suffix: ''
    }]
    var running = true

    return inquirer
        .prompt(question)
        .then(answers => {
            cmd = answers.maxctrl
            if (cmd.toLowerCase() == 'exit' || cmd.toLowerCase() == 'quit') {
                return Promise.resolve()
            } else {
                return doCommand(base_opts.concat(cmd.split(' ')))
                    .then((output) => {
                        if (output) {
                            console.log(output)
                        }
                        return askQuestion()
                    }, (err) => {
                        if (err) {
                            console.log(err)
                        } else {
                            console.log('An undefined error has occurred')
                        }
                        return askQuestion()
                    })

            }
        })
}
