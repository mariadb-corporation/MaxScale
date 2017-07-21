require('../test_utils.js')()

describe("Library invocation", function() {
    before(startMaxScale)

    var ctrl = require('../lib/core.js')
    var opts = { extra_args: [ '--quiet'] }

    it('extra options', function() {
        return ctrl.execute('list servers'.split(' '), opts)
            .should.be.fulfilled
    })

    it('no options', function() {
        return ctrl.execute('list servers'.split(' '))
            .should.be.fulfilled
    })

    after(stopMaxScale)
});
