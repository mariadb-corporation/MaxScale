require('../test_utils.js')()

describe("Module Commands", function() {
    before(startMaxScale)

    it('call command', function() {
        return doCommand('call command qlafilter log QLA')
            .then(function(output) {
                JSON.parse(output).meta.should.have.lengthOf(1)
            })
    })

    it('will not call command with missing parameters', function() {
        return doCommand('call command qlafilter log')
            .should.be.rejected
    })

    it('will not call command with too many parameters', function() {
        return doCommand('call command qlafilter log QLA too many arguments for this command')
            .should.be.rejected
    })

    after(stopMaxScale)
});
