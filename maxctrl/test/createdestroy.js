require('../test_utils.js')()

var ctrl = require('../lib/core.js')
var opts = { extra_args: [ '--quiet'] }

describe("Create/Destroy Commands", function() {
    before(startMaxScale)

    it('create monitor', function() {
        return doCommand('create monitor my-monitor mysqlmon')
            .then(function() {
                return request.get(host + 'monitors/my-monitor', {json: true})
                    .should.be.fulfilled
            })
    })

    it('destroy monitor', function() {
        return doCommand('destroy monitor my-monitor')
            .should.be.fulfilled
    })

    it('destroy the same monitor again', function() {
        return doCommand('destroy monitor my-monitor')
            .should.be.rejected
    })

    it('destroy nonexistent monitor', function() {
        return doCommand('destroy monitor monitor123')
            .should.be.rejected
    })

    it('create monitor with bad parameters', function() {
        return doCommand('create monitor my-monitor some-module')
            .should.be.rejected
    })

    it('create monitor with bad options', function() {
        return doCommand('create monitor my-monitor mysqlmon --this-is-not-an-option')
            .should.be.rejected
    })

    it('create monitor with options', function() {
        return stopMaxScale()
            .then(startMaxScale)
            .then(function() {
                return doCommand('unlink monitor MySQL-Monitor server4')
            })
            .then(function() {
                return verifyCommand('create monitor my-monitor mysqlmon --servers server4 --monitor-user maxuser --monitor-password maxpwd',
                                    'monitors/my-monitor')
            })
            .then(function(res) {
                res.data.relationships.servers.data.length.should.equal(1)
                res.data.relationships.servers.data[0].id.should.equal("server4")
                res.data.attributes.parameters.user.should.equal("maxuser")
                res.data.attributes.parameters.password.should.equal("maxpwd")
            })
    })

    it('create already existing monitor', function() {
        return doCommand('create monitor my-monitor mysqlmon')
            .should.be.rejected
    })

    it('create server', function() {
        return verifyCommand('create server server5 127.0.0.1 3003', 'servers/server5')
            .should.be.fulfilled
    })

    it('destroy server', function() {
        return doCommand('destroy server server5')
            .should.be.fulfilled
    })

    it('create server with bad parameters', function() {
        return doCommand('create server server5 bad parameter')
            .should.be.rejected
    })

    it('create server with bad options', function() {
        return doCommand('create server server5 bad parameter --this-is-not-an-option')
            .should.be.rejected
    })

    it('create server with options', function() {
        return verifyCommand('create server server5 127.0.0.1 3003 --authenticator GSSAPIBackendAuth',
                             'servers/server5')
            .then(function(res) {
                res.data.attributes.parameters.authenticator.should.equal("GSSAPIBackendAuth")
            })
    })

    it('create already existing server', function() {
        return doCommand('create server server1 127.0.0.1 3000')
            .should.be.rejected
    })

    it('destroy nonexistent server', function() {
        return doCommand('destroy server server123')
            .should.be.rejected
    })

    after(stopMaxScale)
});
