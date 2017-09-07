require('../test_utils.js')()

describe("Enable/Disable Commands", function() {
    before(startMaxScale)

    it('enable log-priority', function() {
        return verifyCommand('enable log-priority info', 'maxscale/logs')
            .then(function(res) {
                res.data.attributes.log_priorities.should.include('info')
            })
    })

    it('disable log-priority', function() {
        return verifyCommand('disable log-priority info', 'maxscale/logs')
            .then(function(res) {
                res.data.attributes.log_priorities.should.not.include('info')
            })
    })

    it('will not enable log-priority with bad parameter', function() {
        return doCommand('enable log-priority bad-stuff')
            .should.be.rejected
    })

    it('will not disable log-priority with bad parameter', function() {
        return doCommand('disable log-priority bad-stuff')
            .should.be.rejected
    })

    it('enable account', function() {
        return verifyCommand('enable account test', 'users/unix/test')
            .should.eventually.have.deep.property('data.attributes.account', 'basic')
    })

    it('disable account', function() {
        return doCommand('disable account test')
            .should.be.fulfilled
    })

    it('enable admin account', function() {
        return verifyCommand('enable account test --type=admin', 'users/unix/test')
            .should.eventually.have.deep.property('data.attributes.account', 'admin')
    })

    it('disable admin account', function() {
        return doCommand('disable account test')
            .should.be.fulfilled
    })

    after(stopMaxScale)
});
