require('../test_utils.js')()

describe("API", function() {
    before(startMaxScale)

    it('gets resource', function() {
        return doCommand('api get servers')
            .should.be.fulfilled
    })

    it('gets resource with path', function() {
        return doCommand('api get servers data[0].id')
            .then((res) => {
                js = JSON.parse(res)
                js.should.equal("server1")
            })
    })

    it('sums zero integer values', function() {
        return doCommand('api get servers data[].attributes.statistics.connections --sum')
            .then((res) => {
                js = JSON.parse(res)
                js.should.equal(0)
            })
    })

    it('sums non-zero integer values', function() {
        return doCommand('api get --sum maxscale/threads data[].attributes.stats.reads')
            .then((res) => {
                js = JSON.parse(res)
                js.should.be.a('number')
            })
    })

    it('does not sum string values', function() {
        return doCommand('api get servers data[].id --sum')
            .then((res) => {
                js = JSON.parse(res)
                js.should.deep.equal(["server1", "server2", "server3", "server4"])
            })
    })

    it('does not sum objects', function() {
        return doCommand('api get servers --sum')
            .should.be.fulfilled
    })

    it('does not sum undefined objects', function() {
        return doCommand('api get servers asdf --sum')
            .should.be.fulfilled
    })

    it('ignores unknown command', function() {
        return doCommand('api upgrade')
            .should.be.rejected
    })

    after(stopMaxScale)
});
