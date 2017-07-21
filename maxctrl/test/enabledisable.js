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

    it('enable log-priority with bad parameter', function() {
        return doCommand('enable log-priority bad-stuff')
            .should.be.rejected
    })

    it('disable log-priority with bad parameter', function() {
        return doCommand('disable log-priority bad-stuff')
            .should.be.rejected
    })

    it('enable account', function() {
        return verifyCommand('enable account test', 'users/unix/test')
            .should.be.fulfilled
    })

    it('disable account', function() {
        return doCommand('disable account test')
            .should.be.fulfilled
    })

    after(stopMaxScale)
});
