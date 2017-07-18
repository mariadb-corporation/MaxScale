require('../test_utils.js')()

describe("Server Commands", function() {
    before(startMaxScale)

    var ctrl = require('maxctrl-core')
    var opts = { extra_args: [ '--quiet'] }

    it('create server', function() {
        return ctrl.execute('create server server5 127.0.0.1 3003'.split(' '), opts)
            .then(function() {
                return request.get(host + 'servers/server5', {json: true})
                    .should.be.fulfilled
            })
    })

    it('alter server', function() {
        return ctrl.execute('alter server server5 port 3004'.split(' '), opts)
            .then(function() {
                return request.get(host + 'servers/server5', {json: true})
            })
            .then(function(res) {
                res.data.attributes.parameters.port.should.equal(3004)
            })
    })

    it('destroy server', function() {
        return ctrl.execute('destroy server server5'.split(' '), opts)
            .should.be.fulfilled
    })

    it('create server with bad parameters', function() {
        return ctrl.execute('create server server5 bad parameter'.split(' '), opts)
            .should.be.rejected
    })

    it('create server with bad options', function() {
        return ctrl.execute('create server server5 bad parameter --this-is-not-an-option'.split(' '), opts)
            .should.be.rejected
    })

    it('create server with options', function() {
        return ctrl.execute('create server server5 127.0.0.1 3003 --authenticator GSSAPIBackendAuth'.split(' '), opts)
            .then(function() {
                return request.get(host + 'servers/server5', {json: true})
            })
            .then(function(res) {
                res.data.attributes.parameters.authenticator.should.equal("GSSAPIBackendAuth")
            })
    })

    it('alter server with bad parameters', function() {
        return ctrl.execute('alter server server1 port not-a-port'.split(' '), opts)
            .should.be.rejected
    })

    it('create already existing server', function() {
        return ctrl.execute('create server server1 127.0.0.1 3000'.split(' '), opts)
            .should.be.rejected
    })

    it('alter nonexistent server', function() {
        return ctrl.execute('alter server server123 port 3000'.split(' '), opts)
            .should.be.rejected
    })

    it('destroy nonexistent server', function() {
        return ctrl.execute('destroy server server123'.split(' '), opts)
            .should.be.rejected
    })

    after(stopMaxScale)
});
