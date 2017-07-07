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

require('../common.js')()

exports.command = 'show <command>'
exports.desc = 'Show objects'
exports.handler = function() {}
exports.builder = function(yargs) {
    yargs
        .command('server <server>', 'Show server', {}, function(argv) {
            getResource('servers/' + argv.server, [
                {'Server': 'id'},
                {'Address': 'attributes.parameters.address'},
                {'Port': 'attributes.parameters.port'},
                {'State': 'attributes.state'},
                {'Services': 'relationships.services.data[].id'},
                {'Monitors': 'relationships.monitors.data[].id'},
                {'Master ID': 'attributes.master_id'},
                {'Node ID': 'attributes.node_id'},
                {'Slave Server IDs': 'attributes.slaves'},
                {'Statistics': 'attributes.statistics'}
            ])
        })
        .command('service <service>', 'Show service', {}, function(argv) {
            getResource('services/' + argv.service, [
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
        .command('monitor <monitor>', 'Show monitor', {}, function(argv) {
            getResource('monitors/' + argv.monitor, [
                {'Monitor': 'id'},
                {'State': 'attributes.state'},
                {'Servers': 'relationships.servers.data[].id'},
                {'Parameters': 'attributes.parameters'},
                {'Monitor Diagnostics': 'attributes.monitor_diagnostics'}
            ])
        })
        .command('session <session>', 'Show session', {}, function(argv) {
            getResource('sessions/' + argv.session, [
                {'Id': 'id'},
                {'Service': 'relationships.services.data[].id'},
                {'State': 'attributes.state'},
                {'User': 'attributes.user'},
                {'Host': 'attributes.remote'},
                {'Connected': 'attributes.connected'},
                {'Idle': 'attributes.idle'}
            ])
        })
        .command('filter <filter>', 'Show filter', {}, function(argv) {
            getResource('filters/' + argv.filter, [
                {'Filter': 'id'},
                {'Module': 'attributes.module'},
                {'Services': 'relationships.services.data[].id'},
                {'Parameters': 'attributes.parameters'}
            ])
        })
        .command('module <module>', 'Show loaded module', {}, function(argv) {
            getResource('maxscale/modules/' + argv.module, [
                {'Module': 'id'},
                {'Type': 'attributes.module_type'},
                {'Version': 'attributes.version'},
                {'Maturity': 'attributes.maturity'},
                {'Description': 'attributes.description'},
                {'Parameters': 'attributes.parameters'},
                {'Commands': 'attributes.commands'}
            ])
        })
        .command('maxscale', 'Show MaxScale information', {}, function(argv) {
            getResource('maxscale', [
                {'Version': 'attributes.version'},
                {'Commit': 'attributes.commit'},
                {'Started At': 'attributes.started_at'},
                {'Uptime': 'attributes.uptime'}
            ])
        })
        .usage('Usage: show <command>')
        .help()
        .command('*', 'the default command', {}, () => {
            console.log('Unknown command. See output of `help show` for a list of commands.')
        })
}
