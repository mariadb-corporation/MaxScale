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

            getCollection('servers',
                          ['Server', 'Address', 'Port', 'Connections', 'Status'],
                          ['id',
                           'attributes.parameters.address',
                           'attributes.parameters.port',
                           'attributes.statistics.connections',
                           'attributes.status'])
        })
        .command('services', 'List services', {}, function() {
            getCollection('services',
                          ['Service', 'Router', 'Connections', 'Total Connections', 'Servers'],
                          ['id',
                           'attributes.router',
                           'attributes.connections',
                           'attributes.total_connections',
                           'relationships.servers.data[].id'])
        })
        .command('monitors', 'List monitors', {}, function() {
            getCollection('monitors',
                          ['Monitor', 'Status', 'Servers'],
                          ['id',
                           'attributes.state',
                           'relationships.servers.data[].id'])
        })
        .command('sessions', 'List sessions', {}, function() {
            getCollection('sessions',
                          ['Id', 'Service', 'User', 'Host'],
                          ['id',
                           'relationships.services.data[].id',
                           'attributes.user',
                           'attributes.remote'])
        })
        .command('filters', 'List filters', {}, function() {
            getCollection('filters',
                          ['Filter', 'Service', 'Module'],
                          ['id',
                           'relationships.services.data[].id',
                           'attributes.module'])
        })
        .command('modules', 'List loaded modules', {}, function() {
            getCollection('maxscale/modules',
                          ['Module', 'Type', 'Version'],
                          ['id',
                           'attributes.module_type',
                           'attributes.version'])
        })
    .help()
}
