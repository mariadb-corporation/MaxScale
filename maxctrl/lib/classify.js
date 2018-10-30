/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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

const classify_fields = [
    {'Parse result': 'attributes.parameters.parse_result'},
    {'Type mask': 'attributes.parameters.type_mask'},
    {'Operation': 'attributes.parameters.operation'},
    {'Has where clause': 'attributes.parameters.has_where_clause'},
    {'Fields': 'attributes.parameters.fields'},
    {'Functions': 'attributes.parameters.functions'}
]

exports.command = 'classify <statement>'
exports.desc = 'Classify statement'
exports.handler = function (argv) {
    maxctrl(argv, function (host) {
        return doRequest(host,
                         'maxscale/query_classifier/classify?sql=' + argv.statement,
                         (res) => {
                             var a = res.data.attributes.parameters.functions.map((f) => {
                                 return f.name + ': (' + f.arguments.join(', ') + ')'
                             });

                             res.data.attributes.parameters.functions = a;

                             return formatResource(classify_fields, res.data)
                         })
    })
}
exports.builder = function(yargs) {
    yargs
        .usage('Usage: classify <statement>')
        .help()
        .command('*', 'the default command', {}, function(argv) {
            console.log("*");
            maxctrl(argv, function(host) {
                console.log(argv.statement);
                return error('Unknown command. See output of `help stop` for a list of commands.')
            })
        })
}
