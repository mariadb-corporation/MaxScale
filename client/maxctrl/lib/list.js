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

exports.command = 'list <command>'
exports.desc = 'List objects'
exports.handler = function() {}
exports.builder = function(yargs) {
    yargs
        .command('servers', 'List servers', {}, function() {
            getCollection('servers', [
                {'Server': 'id'},
                {'Address': 'attributes.parameters.address'},
                {'Port': 'attributes.parameters.port'},
                {'Connections': 'attributes.statistics.connections'},
                {'State': 'attributes.state'}
            ])
        })
        .command('services', 'List services', {}, function() {
            getCollection('services',[
                {'Service': 'id'},
                {'Router': 'attributes.router'},
                {'Connections': 'attributes.connections'},
                {'Total Connections': 'attributes.total_connections'},
                {'Servers': 'relationships.servers.data[].id'}
            ])
        })
        .command('monitors', 'List monitors', {}, function() {
            getCollection('monitors', [
                {'Monitor': 'id'},
                {'State': 'attributes.state'},
                {'Servers': 'relationships.servers.data[].id'}
            ])
        })
        .command('sessions', 'List sessions', {}, function() {
            getCollection('sessions',[
                {'Id': 'id'},
                {'Service': 'relationships.services.data[].id'},
                {'User': 'attributes.user'},
                {'Host': 'attributes.remote'}
            ])
        })
        .command('filters', 'List filters', {}, function() {
            getCollection('filters', [
                {'Filter': 'id'},
                {'Service': 'relationships.services.data[].id'},
                {'Module': 'attributes.module'}
            ])
        })
        .command('modules', 'List loaded modules', {}, function() {
            getCollection('maxscale/modules',[
                {'Module':'id'},
                {'Type':'attributes.module_type'},
                {'Version': 'attributes.version'}
            ])
        })
        .command('users', 'List created network users', {}, function() {
            getCollection('users/inet',[
                {'Name':'id'}
            ])
        })
        .command('commands', 'List module commands', {}, function() {
            getCollection('maxscale/modules',[
                {'Module':'id'},
                {'Commands': 'attributes.commands[].id'}
            ])
        })
        .usage('Usage: list <command>')
        .help()
        .command('*', 'the default command', {}, () => {
            console.log('Unknown command. See output of `help list` for a list of commands.')
        })
}
