require('../test_utils.js')()
var cluster = require('../lib/cluster.js')

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

        cluster.haveExtraServices(a, b).should.be.true
        cluster.haveExtraServices(a, a).should.be.false
        cluster.haveExtraServices(b, b).should.be.false
    })

});


describe('Cluster Commands', function() {
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

    it('sync listener creation', function() {
        return doCommand('create listener RW-Split-Router my-listener-2 5999 --hosts 127.0.0.1:8990')
            .then(() => verifyCommand('cluster sync 127.0.0.1:8990 --hosts 127.0.0.1:8989',
                                      'services/RW-Split-Router/listeners/my-listener-2'))
    })

    it('sync after service alteration', function() {
        return doCommand('alter service RW-Split-Router enable_root_user true --hosts 127.0.0.1:8990')
            .then(() => verifyCommand('cluster sync 127.0.0.1:8990 --hosts 127.0.0.1:8989',
                                      'services/RW-Split-Router'))
            .then(function(res) {
                res.data.attributes.parameters.enable_root_user.should.be.true
            })
    })

    it('sync after listener deletion', function() {
        return doCommand('destroy listener RW-Split-Router my-listener-2 --hosts 127.0.0.1:8990')
            .then(() => verifyCommand('cluster sync 127.0.0.1:8990 --hosts 127.0.0.1:8989',
                                      'services/RW-Split-Router/listeners/my-listener-2'))
            .should.be.rejected
    })

    after(stopDoubleMaxScale)
})
