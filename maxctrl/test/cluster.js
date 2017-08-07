require('../test_utils.js')()
var cluster = require('../lib/cluster.js')
var stripAnsi = require('strip-ansi')

describe('Cluster Command Internals', function() {

    it('detect added and removed objects', function() {
        var a = [
            {
                'id': 'server1',
                'type': 'servers',
                'attributes': {
                    'parameters': {
                        'address': '127.0.0.1',
                        'port': 3000
                    }
                }
            }
        ]

        var b = [
            {
                'id': 'server1',
                'type': 'servers',
                'attributes': {
                    'parameters': {
                        'address': '127.0.0.1',
                        'port': 3000
                    }
                }
            },
            {
                'id': 'server2',
                'type': 'servers',
                'attributes': {
                    'parameters': {
                        'address': '127.0.0.1',
                        'port': 3001
                    }
                }
            }
        ]

        var c = [
            {
                'id': 'server1',
                'type': 'servers',
                'attributes': {
                    'parameters': {
                        'address': '127.0.0.1',
                        'port': 3000
                    }
                }
            },
            {
                'id': 'server3',
                'type': 'servers',
                'attributes': {
                    'parameters': {
                        'address': '127.0.0.1',
                        'port': 3002
                    }
                }
            }
        ]
        cluster.getDifference(b, a)[0].id.should.equal('server2')
        cluster.getDifference(c, a)[0].id.should.equal('server3')
        cluster.getDifference(a, b).should.be.empty
        cluster.getDifference(a, c).should.be.empty
        cluster.getDifference(b, c)[0].id.should.equal('server2')
        cluster.getDifference(c, b)[0].id.should.equal('server3')
        cluster.getDifference(a, a).should.be.empty
        cluster.getDifference(b, b).should.be.empty
        cluster.getDifference(c, c).should.be.empty
    })

    it('detect changes in objects', function() {
        var a = [
            {
                'id': 'server1',
                'type': 'servers',
                'attributes': {
                    'parameters': {
                        'address': '127.0.0.1',
                        'port': 3000
                    }
                }
            }
        ]

        var b = [
            {
                'id': 'server1',
                'type': 'servers',
                'attributes': {
                    'parameters': {
                        'address': '127.0.0.1',
                        'port': 3001
                    }
                }
            }
        ]

        cluster.getDifference(a, b).should.be.empty
        cluster.getDifference(b, a).should.be.empty
        cluster.getDifference(a, a).should.be.empty
        cluster.getDifference(b, b).should.be.empty
        var obj = cluster.getChangedObjects(a, b)[0]
        obj.id.should.equal('server1')
        obj.attributes.parameters.port.should.equal(3000)
    })

    it('detect extra services', function() {
        var a = {
            'services': {
                data: [
                    {
                        'id': 'CLI',
                        'type': 'services',
                        'attributes': {
                            'parameters': {}
                        },
                        'relationships': {},
                    }
                ]
            }
        }

        var b = {
            'services': {
                data: [
                    a.services.data[0],
                    {
                        'id': 'CLI2',
                        'type': 'services',
                        'attributes': {
                            'parameters': {

                            },
                        },
                        'relationships': {},
                    }
                ]
            }
        }

        cluster.haveExtraServices(a, b, 'test1', 'test2').should.be.rejected
        expect(cluster.haveExtraServices(a, a, 'test1', 'test2')).to.equal(undefined)
        expect(cluster.haveExtraServices(b, b, 'test1', 'test2')).to.equal(undefined)
    })

});


describe('Cluster Sync', function() {
    before(startDoubleMaxScale)

    it('sync after server creation', function() {
        return doCommand('create server server5 127.0.0.1 3003 --hosts 127.0.0.1:8990')
            .then(() => verifyCommand('cluster sync 127.0.0.1:8990 --hosts 127.0.0.1:8989',
                                      'servers/server5'))
    })

    it('sync after server alteration', function() {
        return doCommand('alter server server2 port 3000 --hosts 127.0.0.1:8990')
            .then(() => verifyCommand('cluster sync 127.0.0.1:8990 --hosts 127.0.0.1:8989',
                                      'servers/server2'))
            .then(function(res) {
                res.data.attributes.parameters.port.should.equal(3000)
            })
    })

    it('sync after server deletion', function() {
        return doCommand('destroy server server5 --hosts 127.0.0.1:8990')
            .then(() => verifyCommand('cluster sync 127.0.0.1:8990 --hosts 127.0.0.1:8989',
                                      'servers/server5'))
            .should.be.rejected
    })

    it('sync after monitor creation', function() {
        return doCommand('create monitor my-monitor-2 mysqlmon --hosts 127.0.0.1:8990')
            .then(() => verifyCommand('cluster sync 127.0.0.1:8990 --hosts 127.0.0.1:8989',
                                      'monitors/my-monitor-2'))
    })

    it('sync after monitor alteration', function() {
        return doCommand('alter monitor MySQL-Monitor monitor_interval 12345 --hosts 127.0.0.1:8990')
            .then(() => verifyCommand('cluster sync 127.0.0.1:8990 --hosts 127.0.0.1:8989',
                                      'monitors/MySQL-Monitor'))
            .then(function(res) {
                res.data.attributes.parameters.monitor_interval.should.equal(12345)
            })
    })

    it('sync after monitor deletion', function() {
        return doCommand('destroy monitor my-monitor-2 --hosts 127.0.0.1:8990')
            .then(() => doCommand('show monitor my-monitor-2  --hosts 127.0.0.1:8989'))
            .then(() => doCommand('show monitor my-monitor-2  --hosts 127.0.0.1:8990').should.be.rejected)
            .then(() => doCommand('cluster sync 127.0.0.1:8990 --hosts 127.0.0.1:8989'))
            .then(() => doCommand('show monitor my-monitor-2  --hosts 127.0.0.1:8989').should.be.rejected)
            .then(() => doCommand('show monitor my-monitor-2  --hosts 127.0.0.1:8990').should.be.rejected)
    })

    it('sync after service alteration', function() {
        return doCommand('alter service RW-Split-Router enable_root_user true --hosts 127.0.0.1:8990')
            .then(() => verifyCommand('cluster sync 127.0.0.1:8990 --hosts 127.0.0.1:8989',
                                      'services/RW-Split-Router'))
            .then(function(res) {
                res.data.attributes.parameters.enable_root_user.should.be.true
            })
    })

    // As the listeners cannot be truly deleted, since there's no code for actually closing a socket at runtime,
    // we do the listener tests last
    it('sync listener creation', function() {
        return doCommand('create listener RW-Split-Router my-listener-2 5999 --hosts 127.0.0.1:8990')
        // As both MaxScales are on the same machine, both can't listen on the same port. The sync should fail due to this
            .then(() => doCommand('cluster sync 127.0.0.1:8990 --hosts 127.0.0.1:8989').should.be.rejected)
        // Create the listener on the second MaxScale to avoid it being synced later on
            .then(() => doCommand('create listener RW-Split-Router my-listener-2 5998 --hosts 127.0.0.1:8989'))
    })

    after(stopDoubleMaxScale)
})

function isJSON(line) {
    return line.match(/['',\[\]{}:]/)
}

function getOperation(line) {
    var op = null
    line = line.trim()

    if (line.match(/Deleted:/)) {
        op = 'removed'
    } else if (line.match(/New:/)){
        op = 'added'
    } else if (line.match(/Changed:/)) {
        op = 'changed'
    }

    return op
}

// Convert a string format diff into a JSON object
function parseDiff(str) {
    var lines = stripAnsi(str).split(require('os').EOL)
    var rval = {}

    while (lines.length > 0) {
        // Operation is first line, object type second
        var op = getOperation(lines.shift())
        var type = lines.shift()
        var obj = ''

        while (lines.length > 0 && isJSON(lines[0]) && getOperation(lines[0]) == null) {
            obj += lines.shift().trim()
        }

        _.set(rval, op + '.' + type, JSON.parse(obj))
    }

    return rval
}

describe('Cluster Diff', function() {
    before(startDoubleMaxScale)

    it('diff after server creation', function() {
        return doCommand('create server server5 127.0.0.1 3003 --hosts 127.0.0.1:8990')
            .then(() => doCommand('cluster diff 127.0.0.1:8990 --hosts 127.0.0.1:8989'))
            .then(function(res) {
                var d = parseDiff(res)
                d.removed.servers.length.should.equal(1)
                d.removed.servers[0].id.should.equal('server5')
            })
            .then(() => doCommand('cluster sync 127.0.0.1:8990 --hosts 127.0.0.1:8989'))
    })

    it('diff after server alteration', function() {
        return doCommand('alter server server2 port 3000 --hosts 127.0.0.1:8990')
            .then(() => doCommand('cluster diff 127.0.0.1:8990 --hosts 127.0.0.1:8989'))
            .then(function(res) {
                var d = parseDiff(res)
                d.changed.servers.length.should.equal(1)
                d.changed.servers[0].id.should.equal('server2')
            })
            .then(() => doCommand('cluster sync 127.0.0.1:8990 --hosts 127.0.0.1:8989'))
    })

    it('diff after server deletion', function() {
        return doCommand('destroy server server5 --hosts 127.0.0.1:8990')
            .then(() => doCommand('cluster diff 127.0.0.1:8990 --hosts 127.0.0.1:8989'))
            .then(function(res) {
                var d = parseDiff(res)
                d.added.servers.length.should.equal(1)
                d.added.servers[0].id.should.equal('server5')
            })
            .then(() => doCommand('cluster sync 127.0.0.1:8990 --hosts 127.0.0.1:8989'))
    })

    after(stopDoubleMaxScale)
})
