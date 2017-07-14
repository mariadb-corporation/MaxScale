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

function addServer(argv, path, targets) {
    maxctrl(argv)
        .doRequest(path, function(res) {
            var servers =_.get(res, 'data.relationships.servers.data', [])

            targets.forEach(function(i){
                servers.push({id: i, type: 'servers'})
            })

            // Update relationships and remove unnecessary parts
            _.set(res, 'data.relationships.servers.data', servers)
            delete res.data.attributes

            return maxctrl(argv)
                .doAsyncRequest(path, null, {method: 'PATCH', body: res})
        })
}

exports.command = 'link <command>'
exports.desc = 'Link objects'
exports.handler = function() {}
exports.builder = function(yargs) {
    yargs
        .command('service <name> <server...>', 'Link servers to a service', {}, function(argv) {
            addServer(argv, 'services/' + argv.name, argv.server)
        })
        .command('monitor <name> <server...>', 'Link servers to a monitor', {}, function(argv) {
            addServer(argv, 'monitors/' + argv.name, argv.server)
        })
        .usage('Usage: link <command>')
        .help()
        .command('*', 'the default command', {}, () => {
            logger.log('Unknown command. See output of `help link` for a list of commands.')
        })
}
