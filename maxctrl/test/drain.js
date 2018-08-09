require('../test_utils.js')()

describe("Draining servers", function() {
    before(startMaxScale)

    it('drains server', function() {
        return doCommand('drain server server1')
            .should.be.fulfilled
    })

    it('checks server is in maintenance', function() {
        // The maintenance state isn't set instantly
        return sleepFor(2000)
            .then(() => doCommand('api get servers/server1 data.attributes.state'))
            .should.eventually.have.string("Maintenance")
    })

    after(stopMaxScale)
});
