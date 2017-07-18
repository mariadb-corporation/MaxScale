require('../test_utils.js')()

describe("Server states", function() {
    before(startMaxScale)

    var ctrl = require('maxctrl-core')
    var opts = { extra_args: [ '--quiet'] }

    it('link servers to a service', function() {
        return ctrl.execute('link service Read-Connection-Router server1 server2 server3 server4'.split(' '), opts)
            .then(function() {
                return request.get(host + 'services/Read-Connection-Router', {json: true})
            })
            .then(function(res) {
                res.data.relationships.servers.data.length.should.equal(4)
                res.data.relationships.servers.data[0].id.should.equal("server1")
                res.data.relationships.servers.data[1].id.should.equal("server2")
                res.data.relationships.servers.data[2].id.should.equal("server3")
                res.data.relationships.servers.data[3].id.should.equal("server4")
            })
    })

    it('link non-existent service to servers', function() {
        return ctrl.execute('link service not-a-service server1 server2 server3 server4'.split(' '), opts)
            .should.be.rejected
    })

    it('unlink servers from a service', function() {
        return ctrl.execute('unlink service Read-Connection-Router server2 server3 server4'.split(' '), opts)
            .then(function() {
                return request.get(host + 'services/Read-Connection-Router', {json: true})
            })
            .then(function(res) {
                res.data.relationships.servers.data.length.should.equal(1)
                res.data.relationships.servers.data[0].id.should.equal("server1")
            })
    })

    it('unlink non-existent service to servers', function() {
        return ctrl.execute('unlink service not-a-service server1 server2 server3 server4'.split(' '), opts)
            .should.be.rejected
    })

    it('alter service parameter', function() {
        return ctrl.execute('alter service Read-Connection-Router user testuser'.split(' '), opts)
            .then(function() {
                return request.get(host + 'services/Read-Connection-Router', {json: true})
            })
            .then(function(res) {
                res.data.attributes.parameters.user.should.equal("testuser")
            })
    })

    it('alter non-existent service parameter', function() {
        return ctrl.execute('alter service Read-Connection-Router turbocharge yes-please'.split(' '), opts)
            .should.be.rejected
    })

    it('alter non-existent service', function() {
        return ctrl.execute('alter service not-a-service user maxuser'.split(' '), opts)
            .should.be.rejected
    })

    it('stop service', function() {
        return ctrl.execute('stop service Read-Connection-Router'.split(' '), opts)
            .then(function() {
                return request.get(host + 'services/Read-Connection-Router', {json: true})
            })
            .then(function(res) {
                res.data.attributes.state.should.equal("Stopped")
            })
    })

    it('start service', function() {
        return ctrl.execute('start service Read-Connection-Router'.split(' '), opts)
            .then(function() {
                return request.get(host + 'services/Read-Connection-Router', {json: true})
            })
            .then(function(res) {
                res.data.attributes.state.should.equal("Started")
            })
    })

    after(stopMaxScale)
});
