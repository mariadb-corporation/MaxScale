require('../test_utils.js')()

describe("Alter Commands", function() {
    before(startMaxScale)

    it('alter server', function() {
        return verifyCommand('alter server server1 port 3004', 'servers/server1')
            .then(function(res) {
                res.data.attributes.parameters.port.should.equal(3004)
            })
    })

    it('will not alter server with bad parameters', function() {
        return doCommand('alter server server1 port not-a-port')
            .should.be.rejected
    })

    it('will not alter nonexistent server', function() {
        return doCommand('alter server server123 port 3000')
            .should.be.rejected
    })

    it('alter monitor', function() {
        return verifyCommand('alter monitor MariaDB-Monitor monitor_interval 1000', 'monitors/MariaDB-Monitor')
            .then(function(res) {
                res.data.attributes.parameters.monitor_interval.should.equal(1000)
            })
    })

    it('will not alter monitor with bad parameters', function() {
        return doCommand('alter monitor MariaDB-Monitor monitor_interval not-a-number')
            .should.be.rejected
    })

    it('will not alter nonexistent monitor', function() {
        return doCommand('alter monitor monitor123 monitor_interval 3000')
            .should.be.rejected
    })

    it('alter service parameter', function() {
        return verifyCommand('alter service Read-Connection-Router user testuser', 'services/Read-Connection-Router')
            .then(function(res) {
                res.data.attributes.parameters.user.should.equal("testuser")
            })
    })

    it('alter service filters', function() {
        return verifyCommand('alter service-filters Read-Connection-Router', 'services/Read-Connection-Router')
            .then(function(res) {
                res.data.relationships.should.not.have.keys("filters")
            })
            .then(() => verifyCommand('alter service-filters Read-Connection-Router QLA', 'services/Read-Connection-Router'))
            .then(function(res) {
                res.data.relationships.filters.data.length.should.equal(1)
            })
    })

    it('will not alter non-existent service parameter', function() {
        return doCommand('alter service Read-Connection-Router turbocharge yes-please')
            .should.be.rejected
    })

    it('will not alter non-existent service', function() {
        return doCommand('alter service not-a-service user maxuser')
            .should.be.rejected
    })

    it('alter logging', function() {
        return verifyCommand('alter logging maxlog false', 'maxscale/logs')
            .then(function() {
                return verifyCommand('alter logging syslog false', 'maxscale/logs')
            })
            .then(function(res) {
                res.data.attributes.parameters.maxlog.should.equal(false)
                res.data.attributes.parameters.syslog.should.equal(false)
            })
    })

    it('will not alter logging with bad parameter', function() {
        doCommand('alter logging some-parameter maybe')
            .should.be.rejectted
    })

    it('alter maxscale', function() {
        return verifyCommand('alter maxscale auth_connect_timeout 5', 'maxscale')
            .then(function(res) {
                res.data.attributes.parameters.auth_connect_timeout.should.equal(5)
            })
    })

    it('will not alter maxscale with bad parameter', function() {
        return doCommand('alter maxscale some_timeout 123')
            .should.be.rejected
    })

    after(stopMaxScale)
});
