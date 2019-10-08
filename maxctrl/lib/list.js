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

const list_servers_fields = [
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
        name: 'Connections',
        path: 'attributes.statistics.connections',
    },
    {
        name: 'State',
        path: 'attributes.state',
    },
    {
        name: 'GTID',
        path: 'attributes.gtid_current_pos',
    }
]

const list_services_fields = [
    {
        name: 'Service',
        path: 'id',
    },
    {
        name: 'Router',
        path: 'attributes.router',
    },
    {
        name: 'Connections',
        path: 'attributes.connections',
    },
    {
        name: 'Total Connections',
        path: 'attributes.total_connections',
    },
    {
        name: 'Servers',
        path: 'relationships.servers.data[].id',
    }
]

const list_listeners_fields = [
    {
        name: 'Name',
        path: 'id',
    },
    {
        name: 'Port',
        path: 'attributes.parameters.port',
    },
    {
        name: 'Host',
        path: 'attributes.parameters.host',
    },
    {
        name: 'State',
        path: 'attributes.state',
    }
]

const list_monitors_fields = [
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
    }
]

const list_sessions_fields = [
    {
        name: 'Id',
        path: 'id',
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
        name: 'Service',
        path: 'relationships.services.data[].id',
    }
]

const list_filters_fields = [
    {
        name: 'Filter',
        path: 'id',
    },
    {
        name: 'Service',
        path: 'relationships.services.data[].id',
    },
    {
        name: 'Module',
        path: 'attributes.module',
    }
]

const list_modules_fields = [
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
    }
]

const list_threads_fields = [
    {
        name: 'Id',
        path: 'id',
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
    }
]

const list_users_fields = [
    {
        name: 'Name',
        path: 'id',
    },
    {
        name: 'Type',
        path: 'type',
    },
    {
        name: 'Privileges',
        path: 'attributes.account',
    },
]

const list_commands_fields = [
    {
        name: 'Module',
        path: 'id',
    },
    {
        name: 'Commands',
        path: 'attributes.commands[].id',
    }
]

exports.command = 'list <command>'
exports.desc = 'List objects'
exports.handler = function() {}
exports.builder = function(yargs) {
    yargs
        .command('servers', 'List servers', function(yargs) {
            return yargs.epilog('List all servers in MaxScale.')
                .usage('Usage: list servers')
        }, function(argv) {
            maxctrl(argv, function(host) {

                // First, get the list of all servers
                return getJson(host, 'servers')
                    .then((res) => {

                        // Build a set of unique monitors, flatten it into an array of strings and
                        // filter out any duplicate or undefined values (from servers that aren't
                        // monitored).
                        var v = _.uniq(_.flatten(_.getPath(res, 'data[].relationships.monitors.data[].id'))).filter(i => i)

                        // Get the server_info object for each monitor (if it defines one)
                        var infos = []
                        var promises = v.map((i) => getJson(host, 'monitors/' + i)
                                             .then((j) => {
                                                 info = _.get(j, 'data.attributes.monitor_diagnostics.server_info')
                                                 if (info) {
                                                     info.forEach((k) => infos.push(k))
                                                 }
                                             }))

                        // Wait for promises to resolve
                        return Promise.all(promises)
                            .then(() => {
                                res.data.forEach((i) => {

                                    // Get the gtid_current_pos value for each server from the server_info list
                                    var idx = infos.findIndex((info) => info.name == i.id)

                                    if (idx != -1 && infos[idx].gtid_current_pos) {
                                        // Found the GTID position of this server
                                        i.attributes.gtid_current_pos = infos[idx].gtid_current_pos
                                    } else {
                                        // Assign an empty value so we always have something to print
                                        i.attributes.gtid_current_pos = ''
                                    }
                                })
                            })
                            .then(() => filterResource(res, list_servers_fields))
                            .then((res) => rawCollectionAsTable(res, list_servers_fields))
                    })
            })
        })
        .command('services', 'List services', function(yargs) {
            return yargs.epilog('List all services and the servers they use.')
                .usage('Usage: list services')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getCollection(host, 'services', list_services_fields)
            })
        })
        .command('listeners <service>', 'List listeners of a service', function(yargs) {
            return yargs.epilog('List listeners for a service.')
                .usage('Usage: list listeners <service>')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getSubCollection(host, 'services/' + argv.service, 'attributes.listeners', list_listeners_fields)
            })
        })
        .command('monitors', 'List monitors', function(yargs) {
            return yargs.epilog('List all monitors in MaxScale.')
                .usage('Usage: list monitors')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getCollection(host, 'monitors', list_monitors_fields)
            })
        })
        .command('sessions', 'List sessions', function(yargs) {
            return yargs.epilog('List all client sessions.')
                .usage('Usage: list sessions')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getCollection(host, 'sessions', list_sessions_fields)
            })
        })
        .command('filters', 'List filters', function(yargs) {
            return yargs.epilog('List all filters in MaxScale.')
                .usage('Usage: list filters')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getCollection(host, 'filters', list_filters_fields)
            })
        })
        .command('modules', 'List loaded modules', function(yargs) {
            return yargs.epilog('List all currently loaded modules.')
                .usage('Usage: list modules')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getCollection(host, 'maxscale/modules', list_modules_fields)
            })
        })
        .command('threads', 'List threads', function(yargs) {
            return yargs.epilog('List all worker threads.')
                .usage('Usage: list threads')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getCollection(host, 'maxscale/threads', list_threads_fields)
            })
        })
        .command('users', 'List created users', function(yargs) {
            return yargs.epilog('List network the users that can be used to connect to the MaxScale REST API' +
                                ' as well as enabled local accounts.')
                .usage('Usage: list users')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getCollection(host, 'users', list_users_fields)
            })
        })
        .command('commands', 'List module commands', function(yargs) {
            return yargs.epilog('List all available module commands.')
                .usage('Usage: list commands')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getCollection(host, 'maxscale/modules', list_commands_fields)
            })
        })
        .usage('Usage: list <command>')
        .help()
        .command('*', 'the default command', {}, function(argv) {
            maxctrl(argv, function(host) {
                return error('Unknown command. See output of `help list` for a list of commands.')
            })
        })
}
