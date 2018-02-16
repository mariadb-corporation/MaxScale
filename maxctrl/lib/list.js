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
                fields = [
                    {'Server': 'id'},
                    {'Address': 'attributes.parameters.address'},
                    {'Port': 'attributes.parameters.port'},
                    {'Connections': 'attributes.statistics.connections'},
                    {'State': 'attributes.state'},
                    {'GTID': 'attributes.gtid_current_pos'}
                ]

                // First, get the list of all servers
                return getJson(host, 'servers')
                    .then((res) => {

                        promises = []

                        // Iterate over all servers, fetching the monitor that "owns" the server
                        res.data.forEach((i) => {

                            // Assign an empty value so we always have something to print
                            i.attributes.gtid_current_pos = ''

                            owner = _.get(i, 'relationships.monitors.data[0].id')
                            if (owner) {

                                // This servers is monitored by a monitor, get the monitor resource
                                promises.push(
                                    getJson(host, 'monitors/' + owner)
                                        .then((res) => {

                                            // Check if the monitor defines a server_info object
                                            info = _.get(res, 'data.attributes.monitor_diagnostics.server_info')

                                            if (info) {

                                                // Monitor defines it, see if we have a GTID value for this server
                                                info.forEach((j) => {
                                                    if (j.name == i.id && j.gtid_current_pos) {
                                                        // Found the server_info object for this server, get the Gtid_Current_Pos from it
                                                        i.attributes.gtid_current_pos = j.gtid_current_pos
                                                    }
                                                })
                                            }
                                        }))
                            }
                        })

                        // We now have all servers as slightly modified resources and possibly a set
                        // of promises that are getting the related monitor resources which need to
                        // complete.
                        return Promise.all(promises)
                            .then(() => filterResource(res, fields))
                            .then((res) => rawCollectionAsTable(res, fields))
                    })

                return getRawCollection(host, 'servers', fields)
                    .then((res) => {
                        res.forEach((i) => {
                            // The server name will be first
                            //console.log(i[0])
                        })
                        return rawCollectionAsTable(res, fields);
                    })
            })
        })
        .command('services', 'List services', function(yargs) {
            return yargs.epilog('List all services and the servers they use.')
                .usage('Usage: list services')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getCollection(host, 'services',[
                    {'Service': 'id'},
                    {'Router': 'attributes.router'},
                    {'Connections': 'attributes.connections'},
                    {'Total Connections': 'attributes.total_connections'},
                    {'Servers': 'relationships.servers.data[].id'}
                ])
            })
        })
        .command('listeners <service>', 'List listeners of a service', function(yargs) {
            return yargs.epilog('List listeners for a service.')
                .usage('Usage: list listeners <service>')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getSubCollection(host, 'services/' + argv.service, 'attributes.listeners', [
                    {'Name': 'id'},
                    {'Port': 'attributes.parameters.port'},
                    {'Host': 'attributes.parameters.host'}
                ])
            })
        })
        .command('monitors', 'List monitors', function(yargs) {
            return yargs.epilog('List all monitors in MaxScale.')
                .usage('Usage: list monitors')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getCollection(host, 'monitors', [
                    {'Monitor': 'id'},
                    {'State': 'attributes.state'},
                    {'Servers': 'relationships.servers.data[].id'}
                ])
            })
        })
        .command('sessions', 'List sessions', function(yargs) {
            return yargs.epilog('List all client sessions.')
                .usage('Usage: list sessions')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getCollection(host, 'sessions',[
                    {'Id': 'id'},
                    {'Service': 'relationships.services.data[].id'},
                    {'User': 'attributes.user'},
                    {'Host': 'attributes.remote'}
                ])
            })
        })
        .command('filters', 'List filters', function(yargs) {
            return yargs.epilog('List all filters in MaxScale.')
                .usage('Usage: list filters')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getCollection(host, 'filters', [
                    {'Filter': 'id'},
                    {'Service': 'relationships.services.data[].id'},
                    {'Module': 'attributes.module'}
                ])
            })
        })
        .command('modules', 'List loaded modules', function(yargs) {
            return yargs.epilog('List all currently loaded modules.')
                .usage('Usage: list modules')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getCollection(host, 'maxscale/modules',[
                    {'Module':'id'},
                    {'Type':'attributes.module_type'},
                    {'Version': 'attributes.version'}
                ])
            })
        })
        .command('users', 'List created network users', function(yargs) {
            return yargs.epilog('List the users that can be used to connect to the MaxScale REST API.')
                .usage('Usage: list users')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getCollection(host, 'users/inet',[
                    {'Name':'id'}
                ])
            })
        })
        .command('commands', 'List module commands', function(yargs) {
            return yargs.epilog('List all available module commands.')
                .usage('Usage: list commands')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getCollection(host, 'maxscale/modules',[
                    {'Module':'id'},
                    {'Commands': 'attributes.commands[].id'}
                ])
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
