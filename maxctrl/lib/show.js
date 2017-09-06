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

exports.command = 'show <command>'
exports.desc = 'Show objects'
exports.handler = function() {}
exports.builder = function(yargs) {
    yargs
        .command('server <server>', 'Show server', function(yargs) {
            return yargs.epilog('Show detailed information about a server. The `Parameters` ' +
                                'field contains the currently configured parameters for this ' +
                                'server. See `help alter server` for more details about altering ' +
                                'server parameters.');
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getResource(host, 'servers/' + argv.server, [
                    {'Server': 'id'},
                    {'Address': 'attributes.parameters.address'},
                    {'Port': 'attributes.parameters.port'},
                    {'State': 'attributes.state'},
                    {'Services': 'relationships.services.data[].id'},
                    {'Monitors': 'relationships.monitors.data[].id'},
                    {'Master ID': 'attributes.master_id'},
                    {'Node ID': 'attributes.node_id'},
                    {'Slave Server IDs': 'attributes.slaves'},
                    {'Statistics': 'attributes.statistics'},
                    {'Parameters': 'attributes.parameters'}
                ])
            })
        })
        .command('service <service>', 'Show service', function(yargs) {
            return yargs.epilog('Show detailed information about a service. The `Parameters` ' +
                                'field contains the currently configured parameters for this ' +
                                'service. See `help alter service` for more details about altering ' +
                                'service parameters.');
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getResource(host, 'services/' + argv.service, [
                    {'Service': 'id'},
                    {'Router': 'attributes.router'},
                    {'State': 'attributes.state'},
                    {'Started At': 'attributes.started'},
                    {'Current Connections': 'attributes.connections'},
                    {'Total Connections': 'attributes.total_connections'},
                    {'Servers': 'relationships.servers.data[].id'},
                    {'Parameters': 'attributes.parameters'},
                    {'Router Diagnostics': 'attributes.router_diagnostics'}
                ])
            })
        })
        .command('monitor <monitor>', 'Show monitor', function(yargs) {
            return yargs.epilog('Show detailed information about a monitor. The `Parameters` ' +
                                'field contains the currently configured parameters for this ' +
                                'monitor. See `help alter monitor` for more details about altering ' +
                                'monitor parameters.');
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getResource(host, 'monitors/' + argv.monitor, [
                    {'Monitor': 'id'},
                    {'State': 'attributes.state'},
                    {'Servers': 'relationships.servers.data[].id'},
                    {'Parameters': 'attributes.parameters'},
                    {'Monitor Diagnostics': 'attributes.monitor_diagnostics'}
                ])
            })
        })
        .command('session <session>', 'Show session', function(yargs) {
            return yargs.epilog('Show detailed information about a single session. ' +
                                'The list of sessions can be retrieved with the ' +
                                '`list sessions` command. The <session> is the session ' +
                                'ID of a particular session.');
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getResource(host, 'sessions/' + argv.session, [
                    {'Id': 'id'},
                    {'Service': 'relationships.services.data[].id'},
                    {'State': 'attributes.state'},
                    {'User': 'attributes.user'},
                    {'Host': 'attributes.remote'},
                    {'Connected': 'attributes.connected'},
                    {'Idle': 'attributes.idle'}
                ])
            })
        })
        .command('filter <filter>', 'Show filter', function(yargs) {
            return yargs.epilog('The list of services that use this filter is show in the `Services` field.');
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getResource(host, 'filters/' + argv.filter, [
                    {'Filter': 'id'},
                    {'Module': 'attributes.module'},
                    {'Services': 'relationships.services.data[].id'},
                    {'Parameters': 'attributes.parameters'}
                ])
            })
        })
        .command('module <module>', 'Show loaded module', function(yargs) {
            return yargs.epilog('This command shows all available parameters as well as ' +
                                'detailed version information of a loaded module.');
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getResource(host, 'maxscale/modules/' + argv.module, [
                    {'Module': 'id'},
                    {'Type': 'attributes.module_type'},
                    {'Version': 'attributes.version'},
                    {'Maturity': 'attributes.maturity'},
                    {'Description': 'attributes.description'},
                    {'Parameters': 'attributes.parameters'},
                    {'Commands': 'attributes.commands'}
                ])
            })
        })
        .command('maxscale', 'Show MaxScale information', function(yargs) {
            return yargs.epilog('See `help alter maxscale` for more details about altering ' +
                                'MaxScale parameters.');
        }, function(argv) {
            maxctrl(argv, function(host) {
                return getResource(host, 'maxscale', [
                    {'Version': 'attributes.version'},
                    {'Commit': 'attributes.commit'},
                    {'Started At': 'attributes.started_at'},
                    {'Uptime': 'attributes.uptime'},
                    {'Parameters': 'attributes.parameters'}
                ])
            })
        })
        .command('logging', 'Show MaxScale logging information', function(yargs) {
            return yargs.epilog('See `help alter logging` for more details about altering ' +
                                'logging parameters.');
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
                                'the parameter descriptions.');
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
