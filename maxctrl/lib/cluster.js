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
    return _.isEqual(_.get(oldVal, 'attributes.parameters'), _.get(newVal, 'attributes.parameters')) &&
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

// Return a list of objects that differ from each other
function getChangedObjects(a, b) {
    var ours = _.intersectionWith(a, b, sameResource)
    var theirs = _.intersectionWith(b, a, sameResource)
    return _.differenceWith(ours, theirs, equalResources)
}

// Check if the diffs add or delete services
function haveExtraServices(src, dest) {
    var newObj = getDifference(src.services.data, dest.services.data)
    var oldObj = getDifference(dest.services.data, src.services.data)
    return newObj.length > 0 || oldObj.length > 0
}

// Resource collections
const collections = [
    'servers',
    'monitors',
    'services',
    'users'
]

// Individual resources
const endpoints = [
    'maxscale',
    'maxscale/logs'
]

// Calculate a diff between two MaxScale servers
function getDiffs(a, b) {
    var src = {}
    var dest = {}
    var promises = []

    collections.forEach(function(i) {
        promises.push(doAsyncRequest(b, i, function(res) {
            dest[i] = res
        }))
        promises.push(doAsyncRequest(a, i, function(res) {
            src[i] = res
        }))
    })

    endpoints.forEach(function(i) {
        promises.push(doAsyncRequest(b, i, function(res) {
            dest[i] = res
        }))
        promises.push(doAsyncRequest(a, i, function(res) {
            src[i] = res
        }))
    })

    return Promise.all(promises)
        .then(function() {
            // We can get the listeners only after we've requested the services
            dest.services.data.forEach(function(i) {
                dest['services/' + i.id + '/listeners'] = { data: i.attributes.listeners }
            })
            src.services.data.forEach(function(i) {
                src['services/' + i.id + '/listeners'] = { data: i.attributes.listeners }
            })
        })
        .then(function() {
            return Promise.resolve([src, dest])
        })
}

exports.haveExtraServices = haveExtraServices
exports.getDifference = getDifference
exports.getChangedObjects = getChangedObjects
exports.command = 'cluster <command>'
exports.desc = 'Cluster objects'
exports.handler = function() {}
exports.builder = function(yargs) {
    yargs
        .command('diff <target>', 'Show difference between host servers and <target>. ' +
                 'Value must be in HOST:PORT format', {}, function(argv) {

                     maxctrl(argv, function(host) {
                         return getDiffs(host, argv.target)
                             .then(function(diffs) {
                                 var output = []
                                 var src = diffs[0]
                                 var dest = diffs[1]

                                 _.uniq(_.concat(Object.keys(src), Object.keys(dest))).forEach(function(i) {
                                     var newObj = getDifference(src[i].data, dest[i].data)
                                     var oldObj = getDifference(dest[i].data, src[i].data)
                                     var changed = getChangedObjects(src[i].data, dest[i].data)

                                     if (newObj.length) {
                                         output.push("New:", i)
                                         output.push(colors.green(JSON.stringify(newObj, null, 4)))
                                     }
                                     if (oldObj.length) {
                                         output.push("Deleted:", i)
                                         output.push(colors.red(JSON.stringify(oldObj, null, 4)))
                                     }
                                     if (changed.length) {
                                         output.push("Changed:", i)
                                         output.push(colors.yellow(JSON.stringify(changed, null, 4)))
                                     }
                                 })
                                 endpoints.forEach(function(i) {
                                     // Treating the resource endpoints as arrays allows the same functions to be used
                                     // to compare individual resources and resource collections
                                     var changed = getChangedObjects([src[i].data], [dest[i].data])
                                     if (changed.length) {
                                         output.push("Changed:", i)
                                         output.push(colors.yellow(JSON.stringify(changed, null, 4)))
                                     }
                                 })

                                 return output.join(require('os').EOL)
                             })
                     })
                 })
        .command('sync <target>', 'Synchronize the cluster with target MaxScale server.', {}, function(argv) {
            maxctrl(argv, function(host) {
                return getDiffs(argv.target, host)
                    .then(function(diffs) {
                        var promises = []
                        var src = diffs[0]
                        var dest = diffs[1]

                        if (haveExtraServices(src, dest)) {
                            return error('Cannot synchronize host `' + host + '` with target `' +
                                         argv.target + '`: New or deleted services on target host, ' +
                                         'both hosts must have the same services.')
                        }

                        // Delete old servers
                        getDifference(dest.servers.data, src.servers.data).forEach(function(i) {
                            // First unlink the servers from all services and monitors
                            promises.push(
                                doAsyncRequest(host, 'servers/' + i.id, null, {method: 'PATCH', body: _.set({}, 'data.relationships', {})})
                                    .then(function() {
                                        return doAsyncRequest(host, 'servers/' + i.id, null, {method: 'DELETE'})
                                    }))
                        })

                        // Add new servers
                        getDifference(src.servers.data, dest.servers.data).forEach(function(i) {
                            // Create the servers without relationships, those are generated when services and
                            // monitors are updated
                            var newserv = {
                                data: {
                                    id: i.id,
                                    type: i.type,
                                    attributes: {
                                        parameters: i.attributes.parameters
                                    }
                                }
                            }
                            promises.push(doAsyncRequest(host, 'servers', null, {method: 'POST', body: newserv}))
                        })
                        return Promise.all(promises)
                            .then(function() {
                                var promises = []
                                // Delete old monitors
                                getDifference(dest.monitors.data, src.monitors.data).forEach(function(i) {
                                    promises.push(
                                        doAsyncRequest(host, 'monitors/' + i.id, null, {
                                            method: 'PATCH', body: _.set({}, 'data.relationships', {})
                                        })
                                            .then(function() {
                                                return doAsyncRequest(host, 'monitors/' + i.id, null, {method: 'DELETE'})
                                            }))
                                })
                                return Promise.all(promises)
                            })
                            .then(function() {
                                var promises = []
                                // Add new monitors
                                getDifference(src.monitors.data, dest.monitors.data).forEach(function(i) {
                                    promises.push(doAsyncRequest(host, 'monitors', null, {method: 'POST', body: {data: i}}))
                                })
                                return Promise.all(promises)
                            })
                            .then(function() {
                                // Add new and delete old listeners
                                var promises = []
                                var all_keys = _.concat(Object.keys(src), Object.keys(dest))
                                var unwanted_keys = _.concat(collections, endpoints)
                                var relevant_keys = _.uniq(_.difference(all_keys, unwanted_keys))

                                relevant_keys.forEach(function(i) {
                                    getDifference(dest[i].data, src[i].data).forEach(function(j) {
                                        promises.push(doAsyncRequest(host, i + '/' + j.id, null, {method: 'DELETE'}))
                                    })
                                    getDifference(src[i].data, dest[i].data).forEach(function(j) {
                                        promises.push(doAsyncRequest(host, i, null, {method: 'POST', body: {data: j}}))
                                    })
                                })

                                return Promise.all(promises)
                            })
                            .then(function() {
                                var promises = []
                                // PATCH all remaining resource collections in src from dest apart from the
                                // user resource as it requires passwords to be entered
                                _.difference(collections, ['users']).forEach(function(i) {
                                    src[i].data.forEach(function(j) {
                                        promises.push(doAsyncRequest(host, i + '/' + j.id, null, {method: 'PATCH', body: {data: j}}))
                                    })
                                })
                                // Do the same for individual resources
                                endpoints.forEach(function(i) {
                                    promises.push(doAsyncRequest(host, i, null, {method: 'PATCH', body: dest[i]}))
                                })
                                return Promise.all(promises)
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
