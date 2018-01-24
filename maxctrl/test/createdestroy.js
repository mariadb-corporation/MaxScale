require('../test_utils.js')()

var ctrl = require('../lib/core.js')
var opts = { extra_args: [ '--quiet'] }

describe("Create/Destroy Commands", function() {
    before(startMaxScale)

    it('create monitor', function() {
        return verifyCommand('create monitor my-monitor mysqlmon', 'monitors/my-monitor')
            .should.be.fulfilled
    })

    it('destroy monitor', function() {
        return doCommand('destroy monitor my-monitor')
            .should.be.fulfilled
            .then(() => doCommand('show monitor my-monitor'))
            .should.be.rejected
    })

    it('will not destroy the same monitor again', function() {
        return doCommand('destroy monitor my-monitor')
            .should.be.rejected
    })

    it('will not destroy nonexistent monitor', function() {
        return doCommand('destroy monitor monitor123')
            .should.be.rejected
    })

    it('will not create monitor with bad parameters', function() {
        return doCommand('create monitor my-monitor some-module')
            .should.be.rejected
    })

    it('will not create monitor with bad options', function() {
        return doCommand('create monitor my-monitor mysqlmon --this-is-not-an-option')
            .should.be.rejected
    })

    it('create monitor with options', function() {
        return doCommand('unlink monitor MariaDB-Monitor server4')
            .then(() => verifyCommand('create monitor my-monitor mysqlmon --servers server4 --monitor-user maxuser --monitor-password maxpwd',
                                    'monitors/my-monitor'))
            .then(function(res) {
                res.data.relationships.servers.data.length.should.equal(1)
                res.data.relationships.servers.data[0].id.should.equal("server4")
                res.data.attributes.parameters.user.should.equal("maxuser")
                res.data.attributes.parameters.password.should.equal("maxpwd")
            })
    })

    it('will not create already existing monitor', function() {
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

    it('will not create server with bad parameters', function() {
        return doCommand('create server server5 bad parameter')
            .should.be.rejected
    })

    it('will not create server with bad options', function() {
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

    it('create server for service and monitor', function() {
        return verifyCommand('create server server6 127.0.0.1 3005 --services RW-Split-Router --monitors MariaDB-Monitor',
                             'servers/server6')
            .then(function(res) {
                res.data.relationships.services.data[0].id.should.equal("RW-Split-Router")
                res.data.relationships.services.data.length.should.equal(1)
                res.data.relationships.monitors.data[0].id.should.equal("MariaDB-Monitor")
                res.data.relationships.monitors.data.length.should.equal(1)
            })
    })

    it('will not create already existing server', function() {
        return doCommand('create server server1 127.0.0.1 3000')
            .should.be.rejected
    })

    it('will not destroy nonexistent server', function() {
        return doCommand('destroy server server123')
            .should.be.rejected
    })

    it('create listener', function() {
        return verifyCommand('create listener RW-Split-Router my-listener 4567',
                            'services/RW-Split-Router/listeners/my-listener')
            .should.be.fulfilled
    })

    it('will not create already existing listener', function() {
        return doCommand('create listener RW-Split-Router my-listener 7890')
            .should.be.rejected
    })

    it('will not create listener with already used port', function() {
        return doCommand('create listener RW-Split-Router my-listener2 4567')
            .should.be.rejected
    })

    it('destroy listener', function() {
        return doCommand('destroy listener RW-Split-Router my-listener')
            .should.be.fulfilled
    })

    it('will not destroy static listener', function() {
        return doCommand('destroy listener RW-Split-Router RW-Split-Listener')
            .should.be.rejected
    })

    it('create user', function() {
        return verifyCommand('create user testuser test', 'users/inet/testuser')
    })

    it('destroy user', function() {
        return doCommand('destroy user testuser')
    })

    it('create admin user', function() {
        return verifyCommand('create user testadmin test --type=admin', 'users/inet/testadmin')
            .then((res) => {
                res.data.attributes.account.should.equal('admin')
            })
    })

    it('destroy admin user', function() {
        return doCommand('destroy user testadmin')
    })

    it('create basic user', function() {
        return verifyCommand('create user testbasic test --type=basic', 'users/inet/testbasic')
            .then((res) => {
                res.data.attributes.account.should.equal('basic')
            })
    })

    it('destroy basic user', function() {
        return doCommand('destroy user testbasic')
    })

    it('create user with bad type', function() {
        return doCommand('create user testadmin test --type=superuser')
            .should.be.rejected
    })

    after(stopMaxScale)
});
