require('../test_utils.js')()
var cluster = require('../lib/cluster.js')

describe('Cluster Commands', function() {
    before(startMaxScale)

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

    after(stopMaxScale)
});
