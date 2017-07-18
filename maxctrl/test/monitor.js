require('../test_utils.js')()

describe("Monitor Commands", function() {
    before(startMaxScale)

    var ctrl = require('maxctrl-core')
    var opts = { extra_args: [ '--quiet'] }

    it('create monitor', function() {
        return ctrl.execute('create monitor my-monitor mysqlmon'.split(' '), opts)
            .then(function() {
                return request.get(host + 'monitors/my-monitor', {json: true})
                    .should.be.fulfilled
            })
    })

    it('destroy monitor', function() {
        return ctrl.execute('destroy monitor my-monitor'.split(' '), opts)
            .should.be.fulfilled
    })

    it('destroy the same monitor again', function() {
        return ctrl.execute('destroy monitor my-monitor'.split(' '), opts)
            .should.be.rejected
    })

    it('destroy nonexistent monitor', function() {
        return ctrl.execute('destroy monitor monitor123'.split(' '), opts)
            .should.be.rejected
    })

    it('create monitor with bad parameters', function() {
        return ctrl.execute('create monitor my-monitor some-module'.split(' '), opts)
            .should.be.rejected
    })

    it('create monitor with bad options', function() {
        return ctrl.execute('create monitor my-monitor mysqlmon --this-is-not-an-option'.split(' '), opts)
            .should.be.rejected
    })

    it('create monitor with options', function() {
        return stopMaxScale()
            .then(startMaxScale)
            .then(function() {
                return ctrl.execute('unlink monitor MySQL-Monitor server4'.split(' '), opts)
            })
            .then(function() {
                return ctrl.execute('create monitor my-monitor mysqlmon --servers server4 --monitor-user maxuser --monitor-password maxpwd'.split(' '), opts)
            })
            .then(function() {
                return request.get(host + 'monitors/my-monitor', {json: true})
            })
            .then(function(res) {
                res.data.relationships.servers.data.length.should.equal(1)
                res.data.relationships.servers.data[0].id.should.equal("server4")
                res.data.attributes.parameters.user.should.equal("maxuser")
                res.data.attributes.parameters.password.should.equal("maxpwd")
            })
    })

    it('alter monitor', function() {
        return ctrl.execute('alter monitor my-monitor monitor_interval 1000'.split(' '), opts)
            .then(function() {
                return request.get(host + 'monitors/my-monitor', {json: true})
            })
            .then(function(res) {
                res.data.attributes.parameters.monitor_interval.should.equal(1000)
            })
    })

    it('alter monitor with bad parameters', function() {
        return ctrl.execute('alter monitor my-monitor monitor_interval not-a-number'.split(' '), opts)
            .should.be.rejected
    })

    it('create already existing monitor', function() {
        return ctrl.execute('create monitor my-monitor mysqlmon'.split(' '), opts)
            .should.be.rejected
    })

    it('alter nonexistent monitor', function() {
        return ctrl.execute('alter monitor monitor123 monitor_interval 3000'.split(' '), opts)
            .should.be.rejected
    })

    it('stop monitor', function() {
        return ctrl.execute('stop monitor my-monitor'.split(' '), opts)
            .then(function() {
                return request.get(host + 'monitors/my-monitor', {json: true})
            })
            .then(function(res) {
                res.data.attributes.state.should.equal("Stopped")
            })
    })

    it('start monitor', function() {
        return ctrl.execute('start monitor my-monitor'.split(' '), opts)
            .then(function() {
                return request.get(host + 'monitors/my-monitor', {json: true})
            })
            .then(function(res) {
                res.data.attributes.state.should.equal("Running")
            })
    })

    after(stopMaxScale)
});
