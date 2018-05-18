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

const server_fields = [
    {'Server': 'id'},
    {'Address': 'attributes.parameters.address'},
    {'Port': 'attributes.parameters.port'},
    {'State': 'attributes.state'},
    {'Last Event': 'attributes.last_event'},
    {'Triggered At': 'attributes.triggered_at'},
    {'Services': 'relationships.services.data[].id'},
    {'Monitors': 'relationships.monitors.data[].id'},
    {'Master ID': 'attributes.master_id'},
    {'Node ID': 'attributes.node_id'},
    {'Slave Server IDs': 'attributes.slaves'},
    {'Statistics': 'attributes.statistics'},
    {'Parameters': 'attributes.parameters'}
]

const service_fields = [
    {'Service': 'id'},
    {'Router': 'attributes.router'},
    {'State': 'attributes.state'},
    {'Started At': 'attributes.started'},
    {'Current Connections': 'attributes.connections'},
    {'Total Connections': 'attributes.total_connections'},
    {'Servers': 'relationships.servers.data[].id'},
    {'Parameters': 'attributes.parameters'},
    {'Router Diagnostics': 'attributes.router_diagnostics'}
]

const monitor_fields = [
    {'Monitor': 'id'},
    {'State': 'attributes.state'},
    {'Servers': 'relationships.servers.data[].id'},
    {'Parameters': 'attributes.parameters'},
    {'Monitor Diagnostics': 'attributes.monitor_diagnostics'}
]

const session_fields = [
    {'Id': 'id'},
    {'Service': 'relationships.services.data[].id'},
    {'State': 'attributes.state'},
    {'User': 'attributes.user'},
    {'Host': 'attributes.remote'},
    {'Connected': 'attributes.connected'},
    {'Idle': 'attributes.idle'},
    {'Connections': 'attributes.connections[].server'},
    {'Connection IDs': 'attributes.connections[].protocol_diagnostics.connection_id'}
]

const filter_fields = [
    {'Filter': 'id'},
    {'Module': 'attributes.module'},
    {'Services': 'relationships.services.data[].id'},
    {'Parameters': 'attributes.parameters'}
]

const module_fields = [
    {'Module': 'id'},
    {'Type': 'attributes.module_type'},
    {'Version': 'attributes.version'},
    {'Maturity': 'attributes.maturity'},
    {'Description': 'attributes.description'},
    {'Parameters': 'attributes.parameters'},
    {'Commands': 'attributes.commands'}
]

exports.command = 'show <command>'
exports.desc = 'Show objects'
exports.handler = function() {}
exports.builder = function(yargs) {
    yargs
        .command('server <server>', 'Show server', function(yargs) {
            return yargs.epilog('Show detailed information about a server. The `Parameters` ' +
                                'field contains the currently configured parameters for this ' +
                                'server. See `help alter server` for more details about altering ' +
                                'server parameters.')
                .usage('Usage: show server <server>')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getResource(host, 'servers/' + argv.server, server_fields)
            })
        })
        .command('servers', 'Show all servers', function(yargs) {
            return yargs.epilog('Show detailed information about all servers.')
                .usage('Usage: show servers')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getCollectionAsResource(host, 'servers/', server_fields)
            })
        })
        .command('service <service>', 'Show service', function(yargs) {
            return yargs.epilog('Show detailed information about a service. The `Parameters` ' +
                                'field contains the currently configured parameters for this ' +
                                'service. See `help alter service` for more details about altering ' +
                                'service parameters.')
                .usage('Usage: show service <service>')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getResource(host, 'services/' + argv.service, service_fields)
            })
        })
        .command('services', 'Show all services', function(yargs) {
            return yargs.epilog('Show detailed information about all services.')
                .usage('Usage: show services')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getCollectionAsResource(host, 'services/', service_fields)
            })
        })
        .command('monitor <monitor>', 'Show monitor', function(yargs) {
            return yargs.epilog('Show detailed information about a monitor. The `Parameters` ' +
                                'field contains the currently configured parameters for this ' +
                                'monitor. See `help alter monitor` for more details about altering ' +
                                'monitor parameters.')
                .usage('Usage: show monitor <monitor>')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getResource(host, 'monitors/' + argv.monitor, monitor_fields)
            })
        })
        .command('monitors', 'Show all monitors', function(yargs) {
            return yargs.epilog('Show detailed information about all monitors.')
                .usage('Usage: show monitors')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getCollectionAsResource(host, 'monitors/', monitor_fields)
            })
        })
        .command('session <session>', 'Show session', function(yargs) {
            return yargs.epilog('Show detailed information about a single session. ' +
                                'The list of sessions can be retrieved with the ' +
                                '`list sessions` command. The <session> is the session ' +
                                'ID of a particular session.\n\n' +
                                'The `Connections` field lists the servers to which ' +
                                'the session is connected and the `Connection IDs` ' +
                                'field lists the IDs for those connections.')
                .usage('Usage: show session <session>')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getResource(host, 'sessions/' + argv.session, session_fields)
            })
        })
        .command('sessions', 'Show all sessions', function(yargs) {
            return yargs.epilog('Show detailed information about all sessions. ' +
                                'See `help show session` for more details.')
                .usage('Usage: show sessions')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getCollectionAsResource(host, 'sessions/', session_fields)
            })
        })
        .command('filter <filter>', 'Show filter', function(yargs) {
            return yargs.epilog('The list of services that use this filter is show in the `Services` field.')
                .usage('Usage: show filter <filter>')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getResource(host, 'filters/' + argv.filter, filter_fields)
            })
        })
        .command('filters', 'Show all filters', function(yargs) {
            return yargs.epilog('Show detailed information of all filters.')
                .usage('Usage: show filters')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getCollectionAsResource(host, 'filters/', filter_fields)
            })
        })
        .command('module <module>', 'Show loaded module', function(yargs) {
            return yargs.epilog('This command shows all available parameters as well as ' +
                                'detailed version information of a loaded module.')
                .usage('Usage: show module <module>')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getResource(host, 'maxscale/modules/' + argv.module, module_fields)
            })
        })
        .command('modules', 'Show all loaded modules', function(yargs) {
            return yargs.epilog('Displays detailed information about all modules.')
                .usage('Usage: show modules')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getCollectionAsResource(host, 'maxscale/modules/', module_fields)
            })
        })
        .command('maxscale', 'Show MaxScale information', function(yargs) {
            return yargs.epilog('See `help alter maxscale` for more details about altering ' +
                                'MaxScale parameters.')
                .usage('Usage: show maxscale')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getResource(host, 'maxscale', [
                    {'Version': 'attributes.version'},
                    {'Commit': 'attributes.commit'},
                    {'Started At': 'attributes.started_at'},
                    {'Activated At': 'attributes.activated_at'},
                    {'Uptime': 'attributes.uptime'},
                    {'Parameters': 'attributes.parameters'}
                ])
            })
        })
        .command('threads', 'Show worker thread information', function(yargs) {
            return yargs.usage('Usage: show threads')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getCollection(host, 'maxscale/threads', [
                    {'ID': 'id'},
                    {'Current File Descriptors': 'attributes.stats.current_descriptors'},
                    {'Total File Descriptors': 'attributes.stats.total_descriptors'},
                    {'Load Percentage (1s)': 'attributes.stats.load.last_second'},
                    {'Load Percentage (1m)': 'attributes.stats.load.last_minute'},
                    {'Load Percentage (1h)': 'attributes.stats.load.last_hour'}
                ])
            })
        })
        .command('logging', 'Show MaxScale logging information', function(yargs) {
            return yargs.epilog('See `help alter logging` for more details about altering ' +
                                'logging parameters.')
                .usage('Usage: show logging')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getResource(host, 'maxscale/logs', [
                    {'Current Log File': 'attributes.log_file'},
                    {'Enabled Log Levels': 'attributes.log_priorities'},
                    {'Parameters': 'attributes.parameters'}
                ])
            })
        })
        .command('commands <module>', 'Show module commands of a module', function(yargs) {
            return yargs.epilog('This command shows the parameters the command expects with ' +
                                'the parameter descriptions.')
                .usage('Usage: show commands <module>')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getSubCollection(host, 'maxscale/modules/' + argv.module, 'attributes.commands', [
                    {'Command': 'id'},
                    {'Parameters': 'attributes.parameters[].type'},
                    {'Descriptions': 'attributes.parameters[].description'}
                ])
            })
        })
        .usage('Usage: show <command>')
        .help()
        .command('*', 'the default command', {}, function(argv) {
            maxctrl(argv, function(host) {
                return error('Unknown command. See output of `help show` for a list of commands.')
            })
        })
}
