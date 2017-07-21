require('../test_utils.js')()

describe("Rotate Commands", function() {
    before(startMaxScale)

    it('rotate logs', function() {
        return doCommand('rotate logs')
            .should.be.fulfilled
    });

    after(stopMaxScale)
});
