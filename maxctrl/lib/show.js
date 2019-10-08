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

const server_fields = [
    {
        name: 'Server',
        path: 'id',
    },
    {
        name: 'Address',
        path: 'attributes.parameters.address',
    },
    {
        name: 'Port',
        path: 'attributes.parameters.port',
    },
    {
        name: 'State',
        path: 'attributes.state',
    },
    {
        name: 'Last Event',
        path: 'attributes.last_event',
    },
    {
        name: 'Triggered At',
        path: 'attributes.triggered_at',
    },
    {
        name: 'Services',
        path: 'relationships.services.data[].id',
    },
    {
        name: 'Monitors',
        path: 'relationships.monitors.data[].id',
    },
    {
        name: 'Master ID',
        path: 'attributes.master_id',
    },
    {
        name: 'Node ID',
        path: 'attributes.node_id',
    },
    {
        name: 'Slave Server IDs',
        path: 'attributes.slaves',
    },
    {
        name: 'Statistics',
        path: 'attributes.statistics',
    },
    {
        name: 'Parameters',
        path: 'attributes.parameters',
}
]

const service_fields = [
    {
        name: 'Service',
        path: 'id',
    },
    {
        name: 'Router',
        path: 'attributes.router',
    },
    {
        name: 'State',
        path: 'attributes.state',
    },
    {
        name: 'Started At',
        path: 'attributes.started',
    },
    {
        name: 'Current Connections',
        path: 'attributes.connections',
    },
    {
        name: 'Total Connections',
        path: 'attributes.total_connections',
    },
    {
        name: 'Servers',
        path: 'relationships.servers.data[].id',
    },
    {
        name: 'Parameters',
        path: 'attributes.parameters',
    },
    {
        name: 'Router Diagnostics',
        path: 'attributes.router_diagnostics',
    }
]

const monitor_fields = [
    {
        name: 'Monitor',
        path: 'id',
    },
    {
        name: 'State',
        path: 'attributes.state',
    },
    {
        name: 'Servers',
        path: 'relationships.servers.data[].id',
    },
    {
        name: 'Parameters',
        path: 'attributes.parameters',
    },
    {
        name: 'Monitor Diagnostics',
        path: 'attributes.monitor_diagnostics',}
]

const session_fields = [
    {
        name: 'Id',
        path: 'id',
    },
    {
        name: 'Service',
        path: 'relationships.services.data[].id',
    },
    {
        name: 'State',
        path: 'attributes.state',
    },
    {
        name: 'User',
        path: 'attributes.user',
    },
    {
        name: 'Host',
        path: 'attributes.remote',
    },
    {
        name: 'Connected',
        path: 'attributes.connected',
    },
    {
        name: 'Idle',
        path: 'attributes.idle',
    },
    {
        name: 'Connections',
        path: 'attributes.connections[].server',
    },
    {
        name: 'Connection IDs',
        path: 'attributes.connections[].protocol_diagnostics.connection_id',
    },
    {
        name: 'Queries',
        path: 'attributes.queries[].statement',
    },
    {
        name: 'Log',
        path: 'attributes.log',
}
]

const filter_fields = [
    {
        name: 'Filter',
        path: 'id',
    },
    {
        name: 'Module',
        path: 'attributes.module',
    },
    {
        name: 'Services',
        path: 'relationships.services.data[].id',
    },
    {
        name: 'Parameters',
        path: 'attributes.parameters',
    }
]

const module_fields = [
    {
        name: 'Module',
        path: 'id',
    },
    {
        name: 'Type',
        path: 'attributes.module_type',
    },
    {
        name: 'Version',
        path: 'attributes.version',
    },
    {
        name: 'Maturity',
        path: 'attributes.maturity',
    },
    {
        name: 'Description',
        path: 'attributes.description',
    },
    {
        name: 'Parameters',
        path: 'attributes.parameters',
    },
    {
        name: 'Commands',
        path: 'attributes.commands',
    }
]

const thread_fields = [
    {
        name: 'Id',
        path: 'id',
    },
    {
        name: 'Accepts',
        path: 'attributes.stats.accepts',
    },
    {
        name: 'Reads',
        path: 'attributes.stats.reads',
    },
    {
        name: 'Writes',
        path: 'attributes.stats.writes',
    },
    {
        name: 'Hangups',
        path: 'attributes.stats.hangups',
    },
    {
        name: 'Errors',
        path: 'attributes.stats.errors',
    },
    {
        name: 'Avg event queue length',
        path: 'attributes.stats.avg_event_queue_length',
    },
    {
        name: 'Max event queue length',
        path: 'attributes.stats.max_event_queue_length',
    },
    {
        name: 'Max exec time',
        path: 'attributes.stats.max_exec_time',
    },
    {
        name: 'Max queue time',
        path: 'attributes.stats.max_queue_time',
    },
    {
        name: 'Current FDs',
        path: 'attributes.stats.current_descriptors',
    },
    {
        name: 'Total FDs',
        path: 'attributes.stats.total_descriptors',
    },
    {
        name: 'Load (1s)',
        path: 'attributes.stats.load.last_second',
    },
    {
        name: 'Load (1m)',
        path: 'attributes.stats.load.last_minute',
    },
    {
        name: 'Load (1h)',
        path: 'attributes.stats.load.last_hour',
    },
    {
        name: 'QC cache size',
        path: 'attributes.stats.query_classifier_cache.size',
    },
    {
        name: 'QC cache inserts',
        path: 'attributes.stats.query_classifier_cache.inserts',
    },
    {
        name: 'QC cache hits',
        path: 'attributes.stats.query_classifier_cache.hits',
    },
    {
        name: 'QC cache misses',
        path: 'attributes.stats.query_classifier_cache.misses',
    },
    {
        name: 'QC cache evictions',
        path: 'attributes.stats.query_classifier_cache.evictions',
    },
]

const show_maxscale_fields = [
    {
        name: 'Version',
        path: 'attributes.version',
    },
    {
        name: 'Commit',
        path: 'attributes.commit',
    },
    {
        name: 'Started At',
        path: 'attributes.started_at',
    },
    {
        name: 'Activated At',
        path: 'attributes.activated_at',
    },
    {
        name: 'Uptime',
        path: 'attributes.uptime',
    },
    {
        name: 'Parameters',
        path: 'attributes.parameters',
    }
]

const show_logging_fields = [
    {
        name: 'Current Log File',
        path: 'attributes.log_file',
    },
    {
        name: 'Enabled Log Levels',
        path: 'attributes.log_priorities',
    },
    {
        name: 'Parameters',
        path: 'attributes.parameters',
    }
]

const show_commands_fields = [
    {
        name: 'Command',
        path: 'id',
    },
    {
        name: 'Parameters',
        path: 'attributes.parameters[].type',
    },
    {
        name: 'Descriptions',
        path: 'attributes.parameters[].description',
    }
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
                return getResource(host, 'maxscale', show_maxscale_fields)
            })
        })
        .command('thread <thread>', 'Show thread', function(yargs) {
            return yargs.epilog('Show detailed information about a worker thread.')
                .usage('Usage: show thread <thread>')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getResource(host, 'maxscale/threads/' + argv.thread, thread_fields)
            })
        })
        .command('threads', 'Show all threads', function(yargs) {
            return yargs.epilog('Show detailed information about all worker threads.')
                .usage('Usage: show threads')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getCollectionAsResource(host, 'maxscale/threads', thread_fields)
            })
        })
        .command('logging', 'Show MaxScale logging information', function(yargs) {
            return yargs.epilog('See `help alter logging` for more details about altering ' +
                                'logging parameters.')
                .usage('Usage: show logging')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getResource(host, 'maxscale/logs', show_logging_fields)
            })
        })
        .command('commands <module>', 'Show module commands of a module', function(yargs) {
            return yargs.epilog('This command shows the parameters the command expects with ' +
                                'the parameter descriptions.')
                .usage('Usage: show commands <module>')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getSubCollection(host, 'maxscale/modules/' + argv.module, 'attributes.commands',
                                        show_commands_fields)
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
