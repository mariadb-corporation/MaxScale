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

function addServer(argv, path, targets) {
    maxctrl(argv, function(host){
        return doRequest(host, path, function(res) {
            var servers =_.get(res, 'data.relationships.servers.data', [])

            targets.forEach(function(i){
                servers.push({id: i, type: 'servers'})
            })

            // Update relationships and remove unnecessary parts
            _.set(res, 'data.relationships.servers.data', servers)
            delete res.data.attributes

            return doAsyncRequest(host, path, null, {method: 'PATCH', body: res})
        })
    })
}

exports.command = 'link <command>'
exports.desc = 'Link objects'
exports.handler = function() {}
exports.builder = function(yargs) {
    yargs
        .command('service <name> <server...>', 'Link servers to a service', function(yargs) {
            return yargs.epilog('This command links servers to a service, making them available ' +
                                'for any connections that use the service. Before a server is ' +
                                'linked to a service, it should be linked to a monitor so that ' +
                                'the server state is up to date. Newly linked server are only ' +
                                'available to new connections, existing connections will use the ' +
                                'old list of servers.');
        }, function(argv) {
            addServer(argv, 'services/' + argv.name, argv.server)
        })
        .command('monitor <name> <server...>', 'Link servers to a monitor', function(yargs) {
            return yargs.epilog('Linking a server to a monitor will add it to the list of servers ' +
                                'that are monitored by that monitor. A server can be monitored by ' +
                                'only one monitor at a time.');
        }, function(argv) {
            addServer(argv, 'monitors/' + argv.name, argv.server)
        })
        .usage('Usage: link <command>')
        .help()
        .command('*', 'the default command', {}, function(argv) {
            maxctrl(argv, function(host) {
                return error('Unknown command. See output of `help link` for a list of commands.')
            })
        })
}
