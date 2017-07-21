require('../test_utils.js')()

var ctrl = require('../lib/core.js')
var opts = { extra_args: [ '--quiet'] }

describe("Start/Stop Commands", function() {
    before(startMaxScale)

    it('stop service', function() {
        return verifyCommand('stop service Read-Connection-Router', 'services/Read-Connection-Router')
            .then(function(res) {
                res.data.attributes.state.should.equal("Stopped")
            })
    })

    it('start service', function() {
        return verifyCommand('start service Read-Connection-Router', 'services/Read-Connection-Router')
            .then(function(res) {
                res.data.attributes.state.should.equal("Started")
            })
    })

    it('stop monitor', function() {
        return verifyCommand('stop monitor MySQL-Monitor', 'monitors/MySQL-Monitor')
            .then(function(res) {
                res.data.attributes.state.should.equal("Stopped")
            })
    })

    it('start monitor', function() {
        return verifyCommand('start monitor MySQL-Monitor', 'monitors/MySQL-Monitor')
            .then(function(res) {
                res.data.attributes.state.should.equal("Running")
            })
    })

    after(stopMaxScale)
});
