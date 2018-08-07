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
require('./common.js')()

// Converts an array of key=value pairs into an object
function to_obj(obj, value) {
    var kv = value.split('=')
    if (kv.length < 2) {
        throw 'Error: Not a key-value parameter: ' + value
    }
    obj[kv[0]] = kv[1]
    return obj
}

exports.command = 'create <command>'
exports.desc = 'Create objects'
exports.handler = function() {}
exports.builder = function(yargs) {
    yargs
    // Common options
        .group(['protocol', 'authenticator', 'authenticator-options', 'tls-key',
                'tls-cert', 'tls-ca-cert', 'tls-version', 'tls-cert-verify-depth'],
               'Common create options:')
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
        .command('server <name> <host> <port>', 'Create a new server', function(yargs) {
            return yargs.epilog('The created server will not be used by any services or monitors ' +
                                'unless the --services or --monitors options are given. The list ' +
                                'of servers a service or a monitor uses can be altered with the ' +
                                '`link` and `unlink` commands.')
                .usage('Usage: create server <name> <host> <port>')
        }, function(argv) {
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
                            'authenticator_options': argv.auth_options,
                            'ssl_key': argv['tls-key'],
                            'ssl_cert': argv['tls-cert'],
                            'ssl_ca_cert': argv['tls-ca-cert'],
                            'ssl_version': argv['tls-version'],
                            'ssl_cert_verify_depth': argv['tls-cert-verify-depth']
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
        .group(['servers', 'monitor-user', 'monitor-password'], 'Create monitor options:')
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
        .command('monitor <name> <module>', 'Create a new monitor', function(yargs) {
            return yargs.epilog('The list of servers given with the --servers option should not ' +
                                'contain any servers that are already monitored by another monitor.')
                .usage('Usage: create monitor <name> <module>')
        }, function(argv) {

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

    // Create service
        .group(['servers', 'filters'], 'Create service options:')
        .option('servers', {
            describe: 'Link the created service to these servers',
            type: 'array'
        })
        .option('filters', {
            describe: 'Link the created service to these filters',
            type: 'array'
        })
        .command('service <name> <router> <params...>', 'Create a new service', function(yargs) {
            return yargs.epilog('The last argument to this command is a list of key=value parameters ' +
                                'given as the service parameters. If the --servers or --filters options ' +
                                'are used, they must be defined after the service parameters.')
                .usage('Usage: service <name> <router> <params...>')
        }, function(argv) {

            var service = {
                'data': {
                    'id': argv.name,
                    'attributes': {
                        'router': argv.router,
                        'parameters': argv.params.reduce(to_obj, {})
                    }
                }
            }

            if (argv.servers) {
                for (i = 0; i < argv.servers.length; i++) {
                    _.set(service, 'data.relationships.servers.data[' + i + ']', {id: argv.servers[i], type: 'servers'})
                }
            }

            if (argv.filters) {
                for (i = 0; i < argv.filters.length; i++) {
                    _.set(service, 'data.relationships.filters.data[' + i + ']', {id: argv.filters[i], type: 'filters'})
                }
            }

            maxctrl(argv, function(host) {
                return doRequest(host, 'services', null, {method: 'POST', body: service})
            })
        })

    // Create filter
        .command('filter <name> <module> [params...]', 'Create a new filter', function(yargs) {
            return yargs.epilog('The last argument to this command is a list of key=value parameters ' +
                                'given as the filter parameters.')
                .usage('Usage: filter <name> <module> [params...]')
        }, function(argv) {

            var filter = {
                'data': {
                    'id': argv.name,
                    'attributes': {
                        'module': argv.module
                    }
                }
            }

            if (argv.params) {
                filter.data.attributes.parameters = argv.params.reduce(to_obj, {})
            }

            maxctrl(argv, function(host) {
                return doRequest(host, 'filters', null, {method: 'POST', body: filter})
            })
        })

    // Create listener
        .group(['interface'], 'Create listener options:')
        .option('interface', {
            describe: 'Interface to listen on',
            type: 'string',
            default: '::'
        })
        .command('listener <service> <name> <port>', 'Create a new listener', function(yargs) {
            return yargs.epilog('The new listener will be taken into use immediately.')
                .usage('Usage: create listener <service> <name> <port>')
        }, function(argv) {

            if (!Number.isInteger(argv.port)) {
                throw "'" + argv.port + "' is not a valid value for port"
            }

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
                            'ssl_cert_verify_depth': argv['tls-cert-verify-depth']
                        }
                    }
                }
            }

            maxctrl(argv, function(host) {
                return doRequest(host, 'services/' + argv.service + '/listeners', null, {method: 'POST', body: listener})
            })
        })
        .group(['type'], 'Create user options:')
        .option('type', {
            describe: 'Type of user to create',
            type: 'string',
            default: 'basic',
            choices: ['admin', 'basic']
        })
        .command('user <name> <password>', 'Create a new network user', function(yargs) {
            return yargs.epilog('The created user can be used with the MaxScale REST API as ' +
                                'well as the MaxAdmin network interface. By default the created ' +
                                'user will have read-only privileges. To make the user an ' +
                                'administrative user, use the `--type=admin` option.')
                .usage('Usage: create user <name> <password>')
        }, function(argv) {

            var user = {
                'data': {
                    'id': argv.name,
                    'type': 'inet',
                    'attributes': {
                        'password': argv.password,
                        'account': argv.type
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
