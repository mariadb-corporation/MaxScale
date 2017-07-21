require('../test_utils.js')()

var ctrl = require('../lib/core.js')
var opts = { extra_args: [ '--quiet'] }

describe("Enable/Disable Commands", function() {
    before(startMaxScale)

    it('disable with bad parameter', function() {
        return doCommand('disable log-priority bad-stuff')
            .should.be.rejected
    })

    it('enable with bad parameter', function() {
        return doCommand('enable log-priority bad-stuff')
            .should.be.rejected
    })

    after(stopMaxScale)
});
