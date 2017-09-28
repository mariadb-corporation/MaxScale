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

function removeServer(argv, path, targets) {
    maxctrl(argv, function(host) {
        return doRequest(host, path, function(res) {
            var servers =_.get(res, 'data.relationships.servers.data', [])

            _.remove(servers, function(i) {
                return targets.indexOf(i.id) != -1
            })

            // Update relationships and remove unnecessary parts
            _.set(res, 'data.relationships.servers.data', servers)
            delete res.data.attributes

            return doAsyncRequest(host, path, null, {method: 'PATCH', body: res})
        })
    })
}

exports.command = 'unlink <command>'
exports.desc = 'Unlink objects'
exports.handler = function() {}
exports.builder = function(yargs) {
    yargs
        .command('service <name> <server...>', 'Unlink servers from a service', function(yargs) {
            return yargs.epilog('This command unlinks servers from a service, removing them from ' +
                                'the list of available servers for that service. New connections to ' +
                                'the service will not use the unlinked servers but existing ' +
                                'connections can still use the servers.')
                .usage('Usage: unlink service <name> <server...>')
        }, function(argv) {
            removeServer(argv, 'services/' + argv.name, argv.server)
        })
        .command('monitor <name> <server...>', 'Unlink servers from a monitor', function(yargs) {
            return yargs.epilog('This command unlinks servers from a monitor, removing them from ' +
                                'the list of monitored servers. The servers will be left in their ' +
                                'current state when they are unlinked from a monitor.')
                .usage('Usage: unlink monitor <name> <server...>')
        }, function(argv) {
            removeServer(argv, 'monitors/' + argv.name, argv.server)
        })
        .usage('Usage: unlink <command>')
        .help()
        .command('*', 'the default command', {}, function(argv) {
            maxctrl(argv, function(host) {
                return error('Unknown command. See output of `help unlink` for a list of commands.')
            })
        })
}
