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

exports.command = 'create <command>'
exports.desc = 'Create objects'
exports.handler = function() {}
exports.builder = function(yargs) {
    yargs
    // Common options
        .group(['protocol', 'authenticator', 'authenticator-options'], 'Common create options:')
        .option('protocol', {
            describe: 'Protocol module name',
            type: 'string'
        })
        .option('authenticator', {
            describe: 'Authenticator module name',
            type: 'string'
        })
        .option('authenticator-options', {
            describe: 'Option string for the authenticator',
            type: 'string'
        })

    // Create server
        .group(['services', 'monitors'], 'Create server options:')
        .option('services', {
            describe: 'Link the created server to these services',
            type: 'array'
        })
        .option('monitors', {
            describe: 'Link the created server to these monitors',
            type: 'array'
        })
        .command('server <name> <host> <port>', 'Create a new server', {}, function(argv) {
            var server = {
                'data': {
                    'id': argv.name,
                    'type': 'servers',
                    'attributes': {
                        'parameters': {
                            'address': argv.host,
                            'port': argv.port,
                            'protocol': argv.protocol,
                            'authenticator': argv.authenticator,
                            'authenticator_options': argv.auth_options
                        }
                    }
                }
            }

            if (argv.services) {
                for (i = 0; i < argv.services.length; i++) {
                    _.set(server, 'data.relationships.services.data[' + i + ']', {id: argv.services[i], type: 'services'})
                }
            }

            if (argv.monitors) {
                for (i = 0; i < argv.monitors.length; i++) {
                    _.set(server, 'data.relationships.monitors.data[' + i + ']', {id: argv.monitors[i], type: 'monitors'})
                }
            }

            maxctrl(argv, function(host) {
                return doRequest(host, 'servers', null, {method: 'POST', body: server})
            })
        })

    // Create monitor
        .group(['servers'], 'Create monitor options:')
        .option('servers', {
            describe: 'Link the created monitor to these servers',
            type: 'array'
        })
        .option('monitor-user', {
            describe: 'Username for the monitor user',
            type: 'string'
        })
        .option('monitor-password', {
            describe: 'Password for the monitor user',
            type: 'string'
        })
        .command('monitor <name> <module>', 'Create a new monitor', {}, function(argv) {

            var monitor = {
                'data': {
                    'id': argv.name,
                    'attributes': {
                        'module': argv.module
                    }
                }
            }

            if (argv.servers) {
                for (i = 0; i < argv.servers.length; i++) {
                    _.set(monitor, 'data.relationships.servers.data[' + i + ']', {id: argv.servers[i], type: 'servers'})
                }
            }

            if (argv.monitorUser) {
                _.set(monitor, 'data.attributes.parameters.user', argv.monitorUser)
            }
            if (argv.monitorPassword) {
                _.set(monitor, 'data.attributes.parameters.password', argv.monitorPassword)
            }

            maxctrl(argv, function(host) {
                return doRequest(host, 'monitors', null, {method: 'POST', body: monitor})
            })
        })

    // Create listener
        .group(['interface', 'tls-key', 'tls-cert', 'tls-ca-cert', 'tls-version', 'tls-cert-verify-depth'], 'Create listener options:')
        .option('interface', {
            describe: 'Interface to listen on',
            type: 'string',
            default: '::'
        })
    // Should these have ssl as a prefix even though SSL isn't supported?
        .option('tls-key', {
            describe: 'Path to TLS key',
            type: 'string'
        })
        .option('tls-cert', {
            describe: 'Path to TLS certificate',
            type: 'string'
        })
        .option('tls-ca-cert', {
            describe: 'Path to TLS CA certificate',
            type: 'string'
        })
        .option('tls-version', {
            describe: 'TLS version to use',
            type: 'string'
        })
        .option('tls-cert-verify-depth', {
            describe: 'TLS certificate verification depth',
            type: 'string'
        })
        .command('listener <service> <name> <port>', 'Create a new listener', {}, function(argv) {

            var listener = {
                'data': {
                    'id': argv.name,
                    'type': 'listeners',
                    'attributes': {
                        'parameters': {
                            'port': argv.port,
                            'address': argv.interface,
                            'protocol': argv.protocol,
                            'authenticator': argv.authenticator,
                            'authenticator_options': argv.auth_options,
                            'ssl_key': argv['tls-key'],
                            'ssl_cert': argv['tls-cert'],
                            'ssl_ca_cert': argv['tls-ca-cert'],
                            'ssl_version': argv['tls-version'],
                            'ssl_cert_verify_depth': argv['tls-cert-verify-depth'],
                        }
                    }
                }
            }

            maxctrl(argv, function(host) {
                return doRequest(host, 'services/' + argv.service + '/listeners', null, {method: 'POST', body: listener})
            })
        })
        .command('user <name> <password>', 'Create a new network user', {}, function(argv) {

            var user = {
                'data': {
                    'id': argv.name,
                    'type': 'inet',
                    'attributes': {
                        'password': argv.password
                    }
                }
            }

            maxctrl(argv, function(host) {
                return doRequest(host, 'users/inet', null, {method: 'POST', body: user})
            })
        })

        .usage('Usage: create <command>')
        .help()
        .command('*', 'the default command', {}, function(argv) {
            maxctrl(argv, function(host) {
                return error('Unknown command. See output of `help create` for a list of commands.')
            })
        })
}
