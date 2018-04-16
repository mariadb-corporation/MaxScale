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

exports.command = 'api <command>'
exports.desc = 'Raw REST API access'
exports.handler = function() {}
exports.builder = function(yargs) {
    yargs
        .command('get <resource> [path]', 'Get raw JSON', function(yargs) {
            return yargs.epilog('Perform a raw REST API call. ' +
                                'The path definition uses JavaScript syntax to extract values.')
                .usage('Usage: get <resource> [path]')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return doRequest(host, argv.resource, (res) => {
                    if (argv.path) {
                        res = _.getPath(res, argv.path, '')
                    }

                    return JSON.stringify(res)
                })
            })
        })
        .usage('Usage: api <command>')
        .help()
        .command('*', 'the default command', {}, function(argv) {
            maxctrl(argv, function(host) {
                return error('Unknown command. See output of `help api` for a list of commands.')
            })
        })
}
