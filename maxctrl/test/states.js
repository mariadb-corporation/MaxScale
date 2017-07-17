require('../test_utils.js')()

describe("Server states", function() {
    before(function() {
        return startMaxScale()
            .then(function() {
                return request.put(host + 'monitors/MySQL-Monitor/stop')
            })
    })

    var ctrl = require('maxctrl-core')
    var opts = { extra_args: [ '--quiet'] }

    it('set correct state', function() {
        return ctrl.execute('set server server2 master'.split(' '), opts)
            .then(function() {
                return request.get(host + 'servers/server2', {json: true})
            })
            .then(function(res) {
                res.data.attributes.state.should.match(/Master/)
            })
    })

    it('clear correct state', function() {
        return ctrl.execute('clear server server2 master'.split(' '), opts)
            .then(function() {
                return request.get(host + 'servers/server2', {json: true})
            })
            .then(function(res) {
                res.data.attributes.state.should.not.match(/Master/)
            })
    })

    it('set incorrect state', function() {
        return ctrl.execute('set server server2 something'.split(' '), opts)
            .should.be.rejected
    })

    it('clear incorrect state', function() {
        return ctrl.execute('clear server server2 something'.split(' '), opts)
            .should.be.rejected
    })

    after(stopMaxScale)
});
