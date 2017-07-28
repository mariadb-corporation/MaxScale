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
var colors = require('colors/safe')

function equalResources(oldVal, newVal) {
    return _.isEqual(oldVal.attributes.parameters, newVal.attributes.parameters) &&
        _.isEqual(_.get(oldVal, 'relationships.servers.data'), _.get(newVal, 'relationships.servers.data')) &&
        _.isEqual(_.get(oldVal, 'relationships.services.data'), _.get(newVal, 'relationships.services.data')) &&
        _.isEqual(_.get(oldVal, 'relationships.monitors.data'), _.get(newVal, 'relationships.monitors.data'))
}

function sameResource(oldVal, newVal) {
    return oldVal.id == newVal.id
}

// Return objets that are in <a> but not in <b>
function getDifference(a, b) {
    return _.differenceWith(a, b, sameResource)
}

function getChangedObjects(a, b) {
    var ours = _.intersectionWith(a, b, sameResource)
    var theirs = _.intersectionWith(b, a, sameResource)
    return _.differenceWith(ours, theirs, equalResources)
}

exports.command = 'cluster <command>'
exports.desc = 'Cluster objects'
exports.handler = function() {}
exports.builder = function(yargs) {
    yargs
        .command('diff <a> <b>', 'Show differences of <a> when compared with <b>. ' +
                 'Values must be in HOST:PORT format', {}, function(argv) {

            // Sort of a hack-ish way to force only one iteration of this command
            argv.hosts = [argv.a]

            maxctrl(argv, function(host) {
                const collections = [
                    'servers',
                    'monitors',
                    'services',
                    'users'
                ]
                const endpoints = [
                    'maxscale',
                    'maxscale/logs'
                ]

                var src = {}
                var dest = {}
                var promises = []

                collections.forEach(function(i) {
                    promises.push(doAsyncRequest(argv.b, i, function(res) {
                        dest[i] = res
                    }))
                    promises.push(doAsyncRequest(argv.a, i, function(res) {
                        src[i] = res
                    }))
                })

                endpoints.forEach(function(i) {
                    promises.push(doAsyncRequest(argv.b, i, function(res) {
                        dest[i] = res
                    }))
                    promises.push(doAsyncRequest(argv.a, i, function(res) {
                        src[i] = res
                    }))
                })

                return Promise.all(promises)
                    .then(function() {
                        collections.forEach(function(i) {
                            var newObj = getDifference(src[i].data, dest[i].data)
                            var oldObj = getDifference(dest[i].data, src[i].data)
                            var changed = getChangedObjects(src[i].data, dest[i].data)

                            if (newObj.length) {
                                logger.log("New:", i)
                                logger.log(colors.green(JSON.stringify(newObj, null, 4)))
                            }
                            if (oldObj.length) {
                                logger.log("Deleted:", i)
                                logger.log(colors.red(JSON.stringify(oldObj, null, 4)))
                            }
                            if (changed.length) {
                                logger.log("Changed:", i)
                                logger.log(colors.yellow(JSON.stringify(changed, null, 4)))
                            }
                        })
                        endpoints.forEach(function(i) {
                            // Treating the resource endpoints as arrays allows
                            // the same functions to be used to compare
                            // individual resources and resource collections
                            var changed = getChangedObjects([src[i].data], [dest[i].data])
                            if (changed.length) {
                                logger.log("Changed:", i)
                                logger.log(colors.yellow(JSON.stringify(changed, null, 4)))
                            }
                        })
                    })
            })
        })
        .usage('Usage: cluster <command>')
        .help()
        .command('*', 'the default command', {}, function(argv) {
            maxctrl(argv, function() {
                return error('Unknown command. See output of `help cluster` for a list of commands.')
            })
        })
}
